#include "types.h"
#include "stat.h"
#include "unistd.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "dirent.h"
#include "errno.h"
#include "string.h"
#include "stdio.h"
#include "limits.h"

int __posix_lstat(const char *path, struct stat *buf);

static int
path_is_dir(const struct stat *st)
{
  if(st == 0)
    return 0;
  if(st->st_type == T_DIR)
    return 1;
  return (st->st_mode & M_IFMT) == M_IFDIR;
}

struct rm_opts {
  int recursive;
  int force;       /* -f: ignore nonexistent, never prompt, exit 0 */
  int interactive; /* -i: prompt before each removal */
  int verbose;     /* -v: print "removed 'path'" */
};

static struct rm_opts g_rmopts;

static void
usage(void)
{
  dprintf(2, "usage: rm [-rRfiv] file...\n");
  exit(1);
}

/*
 * Prompt the user.  Returns 1 if the user answered yes, 0 otherwise.
 */
static int
confirm(const char *msg)
{
  char ans[8];
  int n;

  dprintf(2, "%s", msg);
  n = read(0, ans, sizeof(ans));
  return (n > 0 && (ans[0] == 'y' || ans[0] == 'Y'));
}

/*
 * Remove path recursively (when recursive=1 in opts) or as a plain file.
 * Returns 0 on success, -1 on error, -2 if path is a dir and not recursive.
 */
static int
rm_path(const char *path)
{
  struct stat st;
  DIR *dp;
  struct dirent *de;
  char child[PATH_MAX];
  int plen;
  int ok;
  char prompt[PATH_MAX + 32];

  if(path == 0 || path[0] == '\0')
    return -1;
  if(strcmp(path, ".") == 0 || strcmp(path, "..") == 0) {
    dprintf(2, "rm: refusing to remove '.' or '..'\n");
    return -1;
  }

  if(__posix_lstat(path, &st) < 0) {
    if(errno == ENOENT && g_rmopts.force)
      return 0;   /* -f: silently ignore missing */
    return -1;
  }

  if(!path_is_dir(&st)) {
    /* Regular file / symlink */
    if(g_rmopts.interactive) {
      snprintf(prompt, sizeof(prompt), "rm: remove '%s'? ", path);
      if(!confirm(prompt))
        return 0;
    }
    if(unlink(path) < 0) {
      if(errno == 0)
        errno = EIO;
      return -1;
    }
    if(g_rmopts.verbose)
      dprintf(1, "removed '%s'\n", path);
    return 0;
  }

  /* Directory */
  if(!g_rmopts.recursive)
    return -2;

  if(g_rmopts.interactive) {
    snprintf(prompt, sizeof(prompt), "rm: descend into directory '%s'? ", path);
    if(!confirm(prompt))
      return 0;
  }

  dp = opendir(path);
  if(dp == 0)
    return -1;

  ok = 0;
  plen = strlen(path);
  while((de = readdir(dp)) != 0) {
    int nlen;

    if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;

    nlen = strlen(de->d_name);
    if(plen + 1 + nlen + 1 > (int)sizeof(child)) {
      dprintf(2, "rm: path too long: %s\n", path);
      ok = -1;
      continue;
    }
    memmove(child, path, plen);
    if(plen > 0 && child[plen - 1] != '/')
      child[plen] = '/', child[plen + 1] = '\0';
    else
      child[plen] = '\0';
    strncat(child, de->d_name, sizeof(child) - strlen(child) - 1);

    if(rm_path(child) < 0)
      ok = -1;
  }
  closedir(dp);

  if(ok < 0)
    return -1;

  if(g_rmopts.interactive) {
    snprintf(prompt, sizeof(prompt), "rm: remove directory '%s'? ", path);
    if(!confirm(prompt))
      return 0;
  }

  if(rmdir(path) < 0) {
    if(errno == 0)
      errno = EIO;
    return -1;
  }
  if(g_rmopts.verbose)
    dprintf(1, "removed directory '%s'\n", path);
  return 0;
}

int
main(int argc, char *argv[])
{
  int i;
  int status;

  g_rmopts.recursive   = 0;
  g_rmopts.force       = 0;
  g_rmopts.interactive = 0;
  g_rmopts.verbose     = 0;
  status               = 0;

  /* Parse flags */
  for(i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
    char *f;

    for(f = argv[i] + 1; *f; f++) {
      switch(*f) {
      case 'r':
      case 'R': g_rmopts.recursive   = 1; break;
      case 'f': g_rmopts.force       = 1; g_rmopts.interactive = 0; break;
      case 'i': g_rmopts.interactive = 1; g_rmopts.force       = 0; break;
      case 'v': g_rmopts.verbose     = 1; break;
      case '-': goto done_flags;
      default:
        dprintf(2, "rm: unknown option '-%c'\n", *f);
        usage();
      }
    }
  }
done_flags:

  if(i >= argc) {
    if(!g_rmopts.force)
      usage();
    exit(0);
  }

  for(; i < argc; i++) {
    int ret;

    ret = rm_path(argv[i]);
    if(ret == -2) {
      if(!g_rmopts.force) {
        dprintf(2, "rm: cannot remove '%s': Is a directory\n", argv[i]);
        status = 1;
      }
    } else if(ret < 0) {
      if(!g_rmopts.force) {
        dprintf(2, "rm: cannot remove '%s': %s\n", argv[i], strerror(errno));
        status = 1;
      }
    }
  }

  exit(status);
}
