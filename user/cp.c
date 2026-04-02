#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "dirent.h"
#include "errno.h"
#include "limits.h"
#include "string.h"
#include "stdlib.h"
#include "auxv6/user.h"

#define COPY_BUF_SIZE 4096

// Keep the transfer buffer out of the user stack. cp's call chain already has
// sizable stack frames; a 4 KiB local buffer can overflow and corrupt state.
static char copy_buf[COPY_BUF_SIZE];

struct cp_options {
  int recursive;
  int preserve;
  int force;
  int interactive;
  int verbose;
  int no_deref;
};

static void
usage(void)
{
  dprintf(2, "Usage: cp [-a] [-Rr] [-fiv] [-LPp] source... dest\n");
  exit(1);
}

static const char *
path_basename(const char *path, char *buf, size_t bufsz)
{
  size_t len;
  size_t start;
  size_t blen;

  if(path == 0 || *path == '\0') {
    if(bufsz > 0)
      buf[0] = '\0';
    return buf;
  }

  len = strlen(path);
  while(len > 0 && path[len - 1] == '/')
    len--;
  if(len == 0) {
    if(bufsz > 1) {
      buf[0] = '/';
      buf[1] = '\0';
    } else if(bufsz > 0) {
      buf[0] = '\0';
    }
    return buf;
  }

  start = len;
  while(start > 0 && path[start - 1] != '/')
    start--;
  blen = len - start;
  if(blen + 1 > bufsz)
    blen = (bufsz > 0) ? bufsz - 1 : 0;
  if(bufsz > 0) {
    memmove(buf, path + start, blen);
    buf[blen] = '\0';
  }
  return buf;
}

static int
path_join(char *dst, size_t dstsz, const char *dir, const char *base)
{
  size_t dlen;
  size_t blen;
  int need_sep;
  size_t total;

  if(dst == 0 || dir == 0 || base == 0)
    return -1;

  dlen = strlen(dir);
  blen = strlen(base);
  need_sep = (dlen > 0 && dir[dlen - 1] != '/');
  total = dlen + (need_sep ? 1 : 0) + blen + 1;
  if(total > dstsz)
    return -1;

  if(dlen)
    memmove(dst, dir, dlen);
  if(need_sep)
    dst[dlen++] = '/';
  if(blen)
    memmove(dst + dlen, base, blen);
  dst[dlen + blen] = '\0';
  return 0;
}

static int
confirm_overwrite(const char *path)
{
  char buf[8];
  int n;

  dprintf(2, "cp: overwrite %s? ", path);
  n = read(0, buf, sizeof(buf) - 1);
  if(n <= 0)
    return 0;
  buf[n] = '\0';
  return (buf[0] == 'y' || buf[0] == 'Y');
}

static int
copy_file_data(int src_fd, int dst_fd, int *bytes_out)
{
  int n;
  int total;

  total = 0;

  while((n = read(src_fd, copy_buf, sizeof(copy_buf))) > 0) {
    int off;

    off = 0;
    while(off < n) {
      int w = write(dst_fd, copy_buf + off, n - off);
      if(w <= 0)
        return -1;
      off += w;
    }
    total += n;
  }

  if(n < 0)
    return -1;
  if(bytes_out)
    *bytes_out = total;
  return 0;
}

static int
copy_file(const char *src, const char *dst, const struct stat *st,
          const struct cp_options *opts)
{
  struct stat dstst;
  int dst_exists;
  int src_fd;
  int dst_fd;
  int rc;
  int copied;

  dst_exists = (stat(dst, &dstst) == 0);
  if(dst_exists && dstst.st_type == T_DIR) {
    dprintf(2, "cp: %s is a directory\n", dst);
    return -1;
  }

  if(dst_exists && dstst.st_dev == st->st_dev && dstst.st_ino == st->st_ino) {
    dprintf(2, "cp: %s and %s are the same file\n", src, dst);
    return -1;
  }

  if(dst_exists && opts->interactive && !confirm_overwrite(dst))
    return 0;

  if(opts->force && dst_exists)
    unlink(dst);

  src_fd = open(src, O_RDONLY);
  if(src_fd < 0) {
    dprintf(2, "cp: cannot open %s\n", src);
    return -1;
  }

  dst_fd = open(dst, O_WRONLY | O_CREATE | O_TRUNC);
  if(dst_fd < 0) {
    close(src_fd);
    dprintf(2, "cp: cannot create %s\n", dst);
    return -1;
  }

  copied = 0;
  rc = copy_file_data(src_fd, dst_fd, &copied);
  if(rc == 0 && copied == 0 && st->st_size > 0) {
    if(lseek(src_fd, 0, SEEK_SET) >= 0 && lseek(dst_fd, 0, SEEK_SET) >= 0)
      rc = copy_file_data(src_fd, dst_fd, &copied);
  }
  close(src_fd);
  close(dst_fd);
  if(rc < 0) {
    dprintf(2, "cp: failed to copy %s\n", src);
    return -1;
  }
  if(copied == 0 && st->st_size > 0) {
    dprintf(2, "cp: read returned no data for %s\n", src);
    return -1;
  }

  if(opts->preserve) {
    chmod(dst, st->st_mode & 07777);
    chown(dst, st->st_uid, st->st_gid);
  }

  if(opts->verbose)
    dprintf(1, "%s -> %s\n", src, dst);

  return 0;
}

static int
copy_symlink(const char *src, const char *dst, const struct stat *st,
             const struct cp_options *opts)
{
  struct stat dstst;
  char linkbuf[PATH_MAX];
  int dst_exists;
  int n;

  dst_exists = (lstat(dst, &dstst) == 0);
  if(dst_exists && dstst.st_type == T_DIR) {
    dprintf(2, "cp: %s is a directory\n", dst);
    return -1;
  }

  if(dst_exists && opts->interactive && !confirm_overwrite(dst))
    return 0;

  if(opts->force && dst_exists)
    unlink(dst);

  n = readlink(src, linkbuf, sizeof(linkbuf) - 1);
  if(n < 0) {
    dprintf(2, "cp: cannot read link %s\n", src);
    return -1;
  }
  linkbuf[n] = '\0';

  if(symlink(linkbuf, dst) < 0) {
    dprintf(2, "cp: cannot create symlink %s\n", dst);
    return -1;
  }

  if(opts->verbose)
    dprintf(1, "%s -> %s\n", src, dst);

  return 0;
}

static int
copy_device(const char *src, const char *dst, const struct stat *st,
            const struct cp_options *opts)
{
  struct stat dstst;
  int dst_exists;

  dst_exists = (stat(dst, &dstst) == 0);
  if(dst_exists && dstst.st_type == T_DIR) {
    dprintf(2, "cp: %s is a directory\n", dst);
    return -1;
  }

  if(dst_exists && opts->interactive && !confirm_overwrite(dst))
    return 0;

  if(opts->force && dst_exists)
    unlink(dst);

  if(mknod(dst, st->st_mode, st->st_major, st->st_minor) < 0) {
    dprintf(2, "cp: cannot create device %s\n", dst);
    return -1;
  }

  if(opts->preserve) {
    chmod(dst, st->st_mode & 07777);
    chown(dst, st->st_uid, st->st_gid);
  }

  if(opts->verbose)
    dprintf(1, "%s -> %s\n", src, dst);

  return 0;
}

static int copy_entry(const char *src, const char *dst,
                      const struct cp_options *opts);

static int
copy_dir(const char *src, const char *dst, const struct stat *st,
         const struct cp_options *opts)
{
  struct stat dstst;
  int dst_exists;
  DIR *dirp;
  struct dirent *de;
  char src_real[PATH_MAX];
  char dst_real[PATH_MAX];
  int status;

  dst_exists = (stat(dst, &dstst) == 0);
  if(dst_exists && dstst.st_type != T_DIR) {
    dprintf(2, "cp: %s is not a directory\n", dst);
    return -1;
  }

  if(!dst_exists) {
    if(mkdir(dst) < 0) {
      dprintf(2, "cp: cannot create directory %s\n", dst);
      return -1;
    }
  }

  if(realpath(src, src_real) && realpath(dst, dst_real)) {
    size_t srclen = strlen(src_real);
    if(strcmp(src_real, dst_real) == 0 ||
       (strncmp(dst_real, src_real, srclen) == 0 && dst_real[srclen] == '/')) {
      dprintf(2, "cp: cannot copy %s into %s\n", src, dst);
      return -1;
    }
  }

  if(opts->verbose)
    dprintf(1, "%s -> %s\n", src, dst);

  dirp = opendir(src);
  if(dirp == 0) {
    dprintf(2, "cp: cannot open directory %s\n", src);
    return -1;
  }

  status = 0;
  while((de = readdir(dirp)) != 0) {
    char srcpath[PATH_MAX];
    char dstpath[PATH_MAX];

    if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;

    if(path_join(srcpath, sizeof(srcpath), src, de->d_name) < 0 ||
       path_join(dstpath, sizeof(dstpath), dst, de->d_name) < 0) {
      dprintf(2, "cp: path too long under %s\n", src);
      status = -1;
      break;
    }

    if(copy_entry(srcpath, dstpath, opts) < 0)
      status = -1;
  }

  closedir(dirp);

  if(opts->preserve) {
    chmod(dst, st->st_mode & 07777);
    chown(dst, st->st_uid, st->st_gid);
  }

  return status;
}

static int
copy_entry(const char *src, const char *dst, const struct cp_options *opts)
{
  struct stat st;

  if(opts->no_deref) {
    if(lstat(src, &st) < 0) {
      dprintf(2, "cp: cannot stat %s\n", src);
      return -1;
    }
  } else {
    if(stat(src, &st) < 0) {
      dprintf(2, "cp: cannot stat %s\n", src);
      return -1;
    }
  }

  if(st.st_type == T_DIR) {
    if(!opts->recursive) {
      dprintf(2, "cp: %s is a directory\n", src);
      return -1;
    }
    return copy_dir(src, dst, &st, opts);
  }

  if(st.st_type == T_SYMLINK && opts->no_deref)
    return copy_symlink(src, dst, &st, opts);

  if(st.st_type == T_DEV)
    return copy_device(src, dst, &st, opts);

  return copy_file(src, dst, &st, opts);
}

int
main(int argc, char *argv[])
{
  struct cp_options opts;
  int i;
  int src_count;
  char *dest;
  struct stat dstst;
  int dest_is_dir;
  int status;

  memset(&opts, 0, sizeof(opts));

  i = 1;
  while(i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
    char *arg = argv[i];
    int j;

    if(strcmp(arg, "--") == 0) {
      i++;
      break;
    }

    for(j = 1; arg[j] != '\0'; j++) {
      switch(arg[j]) {
      case 'a':
        opts.recursive = 1;
        opts.preserve = 1;
        opts.no_deref = 1;
        break;
      case 'r':
      case 'R':
        opts.recursive = 1;
        break;
      case 'p':
        opts.preserve = 1;
        break;
      case 'f':
        opts.force = 1;
        opts.interactive = 0;
        break;
      case 'i':
        opts.interactive = 1;
        opts.force = 0;
        break;
      case 'v':
        opts.verbose = 1;
        break;
      case 'P':
        opts.no_deref = 1;
        break;
      case 'L':
        opts.no_deref = 0;
        break;
      default:
        usage();
      }
    }
    i++;
  }

  src_count = argc - i;
  if(src_count < 2)
    usage();

  dest = argv[argc - 1];
  dest_is_dir = (stat(dest, &dstst) == 0 && dstst.st_type == T_DIR);

  if(src_count > 2 && !dest_is_dir) {
    dprintf(2, "cp: %s is not a directory\n", dest);
    exit(1);
  }

  status = 0;
  for(; i < argc - 1; i++) {
    char dstpath[PATH_MAX];
    const char *target;

    if(dest_is_dir) {
      char base[NAME_MAX + 1];

      path_basename(argv[i], base, sizeof(base));
      if(path_join(dstpath, sizeof(dstpath), dest, base) < 0) {
        dprintf(2, "cp: path too long for %s\n", argv[i]);
        status = 1;
        continue;
      }
      target = dstpath;
    } else {
      target = dest;
    }

    if(copy_entry(argv[i], target, &opts) < 0)
      status = 1;
  }

  exit(status);
}
