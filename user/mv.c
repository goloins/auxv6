#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "errno.h"
#include "limits.h"
#include "string.h"
#include "stdlib.h"
#include "libgen.h"
#include "auxv6/user.h"

#define COPY_BUF_SIZE 4096

static char mv_buf[COPY_BUF_SIZE];

struct mv_opts {
  int force;
  int interactive;
  int verbose;
};

static void
usage(void)
{
  dprintf(2, "usage: mv [-fiv] source... dest\n");
  exit(1);
}

/*
 * Build dest_path = dir/leaf(src).  Returns 0 on success, -1 if the
 * resulting path would overflow buf.
 */
static int
build_dest(char *buf, size_t bufsz, const char *dir, const char *src)
{
  char tmp[PATH_MAX];
  char *base;
  size_t dlen;
  size_t blen;
  int need_sep;

  strncpy(tmp, src, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';
  base = basename(tmp);

  dlen = strlen(dir);
  blen = strlen(base);
  need_sep = (dlen > 0 && dir[dlen - 1] != '/');

  if(dlen + (need_sep ? 1 : 0) + blen + 1 > bufsz)
    return -1;

  memmove(buf, dir, dlen);
  if(need_sep)
    buf[dlen++] = '/';
  memmove(buf + dlen, base, blen + 1);
  return 0;
}

/*
 * Copy src to dst by reading and writing.  Used as a fallback for
 * cross-device rename failures.
 */
static int
copy_file(const char *src, const char *dst, struct stat *srcst)
{
  int sfd;
  int dfd;
  int n;

  sfd = open(src, O_RDONLY);
  if(sfd < 0)
    return -1;

  dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
  if(dfd < 0) {
    close(sfd);
    return -1;
  }

  while((n = read(sfd, mv_buf, sizeof(mv_buf))) > 0) {
    if(write(dfd, mv_buf, n) != n) {
      close(sfd);
      close(dfd);
      return -1;
    }
  }

  close(sfd);
  close(dfd);

  if(n < 0)
    return -1;

  /* Preserve permissions */
  chmod(dst, srcst->st_mode & 0777);

  return 0;
}

static int
do_move(const char *src, const char *dst, const struct mv_opts *opts)
{
  struct stat dstst;
  struct stat srcst;
  int has_dst;
  char ans[8];

  if(stat(src, &srcst) < 0) {
    dprintf(2, "mv: cannot stat '%s': %s\n", src, strerror(errno));
    return 1;
  }

  has_dst = (stat(dst, &dstst) == 0);

  if(has_dst && !opts->force) {
    if(opts->interactive) {
      dprintf(2, "mv: overwrite '%s'? ", dst);
      if(read(0, ans, sizeof(ans)) <= 0 || (ans[0] != 'y' && ans[0] != 'Y'))
        return 0;
    }
  }

  if(rename(src, dst) == 0) {
    if(opts->verbose)
      dprintf(1, "'%s' -> '%s'\n", src, dst);
    return 0;
  }

  /* rename(2) fails across devices with EXDEV; fall back to copy+unlink */
  if(errno != EXDEV) {
    dprintf(2, "mv: cannot move '%s' to '%s': %s\n", src, dst, strerror(errno));
    return 1;
  }

  if(copy_file(src, dst, &srcst) < 0) {
    dprintf(2, "mv: cannot move '%s' to '%s': copy failed\n", src, dst);
    return 1;
  }

  if(unlink(src) < 0) {
    dprintf(2, "mv: warning: copied '%s' but could not remove source\n", src);
    return 1;
  }

  if(opts->verbose)
    dprintf(1, "'%s' -> '%s'\n", src, dst);
  return 0;
}

int
main(int argc, char *argv[])
{
  struct mv_opts opts;
  int i;
  int status;
  int nsrcs;
  char *dest;
  struct stat destst;
  int dest_is_dir;
  char destbuf[PATH_MAX];

  opts.force       = 0;
  opts.interactive = 0;
  opts.verbose     = 0;
  status           = 0;

  /* Parse flags */
  for(i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
    char *f;

    for(f = argv[i] + 1; *f; f++) {
      switch(*f) {
      case 'f': opts.force       = 1; opts.interactive = 0; break;
      case 'i': opts.interactive = 1; opts.force       = 0; break;
      case 'v': opts.verbose     = 1; break;
      default:
        dprintf(2, "mv: unknown option '-%c'\n", *f);
        usage();
      }
    }
  }

  /* Remaining args: sources + dest */
  nsrcs = argc - i - 1;
  if(nsrcs < 1) {
    usage();
  }
  dest = argv[argc - 1];

  dest_is_dir = (stat(dest, &destst) == 0 && destst.st_type == T_DIR);

  if(nsrcs > 1 && !dest_is_dir) {
    dprintf(2, "mv: target '%s' is not a directory\n", dest);
    exit(1);
  }

  for(; i < argc - 1; i++) {
    const char *src = argv[i];
    const char *dst;

    if(dest_is_dir) {
      if(build_dest(destbuf, sizeof(destbuf), dest, src) < 0) {
        dprintf(2, "mv: destination path too long\n");
        status = 1;
        continue;
      }
      dst = destbuf;
    } else {
      dst = dest;
    }

    if(do_move(src, dst, &opts) != 0)
      status = 1;
  }

  exit(status);
}

