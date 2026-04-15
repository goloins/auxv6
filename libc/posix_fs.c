/*
 * posix_fs.c - path and filesystem wrappers split out of user/posix.c
 */

#include "types.h"
#include "fcntl.h"
#include "sys/stat.h"
#include "sys/uio.h"
#include "poll.h"
#include "dirent.h"
#include "errno.h"
#include "stdarg.h"
#include "string.h"
#include "time.h"
#include "utime.h"
#include "signal.h"
#include "auxv6/user.h"

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif

#ifndef AT_EACCESS
#define AT_EACCESS 0x200
#endif

#ifndef F_OK
#define F_OK 0
#endif

#ifndef X_OK
#define X_OK 1
#endif

#ifndef W_OK
#define W_OK 2
#endif

#ifndef R_OK
#define R_OK 4
#endif

uid_t geteuid(void);
gid_t getegid(void);

/*
 * Kernel syscall ABI struct for stat-family calls.
 * This mirrors include/stat.h layout and stays private to libc.
 */
struct auxv6_kstat {
  short st_type;
  int st_dev;
  uint st_ino;
  short st_major;
  short st_minor;
  short st_nlink;
  short st_uid;
  short st_gid;
  ushort st_mode;
  uint64_t st_size;
  long st_atime;
  long st_mtime;
  long st_ctime;
};

#define AUX_T_DIR 1
#define AUX_T_FILE 2
#define AUX_T_DEV 3
#define AUX_T_SYMLINK 4

int __auxv6_sys_open(const char *path, int flags);
ssize_t __auxv6_sys_read(int fd, void *buf, size_t count);
ssize_t __auxv6_sys_write(int fd, const void *buf, size_t count);
int __auxv6_sys_close(int fd);
int __auxv6_sys_fstat(int fd, struct auxv6_kstat *st);
int __auxv6_sys_stat(const char *path, struct auxv6_kstat *st);
int __auxv6_sys_lstat(const char *path, struct auxv6_kstat *st);
int __auxv6_sys_chdir(const char *path);
int __auxv6_sys_dup(int fd);
int __auxv6_sys_dup2(int oldfd, int newfd);
off_t __auxv6_sys_lseek(int fd, off_t offset, int whence);
int __auxv6_sys_fcntl(int fd, int cmd, int arg);
int __auxv6_sys_ioctl(int fd, int request, void *argp);
ssize_t __auxv6_sys_readlink(const char *path, char *buf, size_t bufsiz);
int __auxv6_sys_utimensat(int dirfd, const char *path,
                          const struct timespec *times, int flags);
int __auxv6_sys_fchmod(int fd, mode_t mode);

static int
posix_valid_utimens_nsec(long nsec)
{
  if(nsec == UTIME_NOW || nsec == UTIME_OMIT)
    return 1;
  return (nsec >= 0 && nsec < NSEC_PER_SEC);
}

static int
posix_fail_errno(int fallback)
{
  if(errno == 0)
    errno = fallback;
  return -1;
}

int
open(const char *path, int flags, ...)
{
  va_list ap;
  int rc;

  va_start(ap, flags);
  va_end(ap);

  if(path == 0) {
    errno = EINVAL;
    return -1;
  }

  errno = 0;
  rc = __auxv6_sys_open(path, flags);
  if(rc < 0)
    return posix_fail_errno(ENOENT);
  return rc;
}

int
creat(const char *path, int mode)
{
  return open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

ssize_t
read(int fd, void *buf, size_t count)
{
  ssize_t rc;

  errno = 0;
  rc = __auxv6_sys_read(fd, buf, count);
  if(rc < 0)
    return posix_fail_errno(EBADF);
  return rc;
}

ssize_t
write(int fd, const void *buf, size_t count)
{
  ssize_t rc;

  errno = 0;
  rc = __auxv6_sys_write(fd, buf, count);
  if(rc < 0)
    return posix_fail_errno(EBADF);
  return rc;
}

ssize_t
readv(int fd, const struct iovec *iov, int iovcnt)
{
  int i;
  ssize_t total;

  if(iov == 0 || iovcnt < 0) {
    errno = EINVAL;
    return -1;
  }

  total = 0;
  for(i = 0; i < iovcnt; i++) {
    ssize_t n;

    if(iov[i].iov_len == 0)
      continue;
    n = read(fd, iov[i].iov_base, iov[i].iov_len);
    if(n < 0)
      return (total > 0) ? total : -1;
    total += n;
    if((size_t)n < iov[i].iov_len)
      break;
  }
  return total;
}

ssize_t
writev(int fd, const struct iovec *iov, int iovcnt)
{
  int i;
  ssize_t total;

  if(iov == 0 || iovcnt < 0) {
    errno = EINVAL;
    return -1;
  }

  total = 0;
  for(i = 0; i < iovcnt; i++) {
    ssize_t n;

    if(iov[i].iov_len == 0)
      continue;
    n = write(fd, iov[i].iov_base, iov[i].iov_len);
    if(n < 0)
      return (total > 0) ? total : -1;
    total += n;
    if((size_t)n < iov[i].iov_len)
      break;
  }
  return total;
}

int
close(int fd)
{
  int rc;

  errno = 0;
  rc = __auxv6_sys_close(fd);
  if(rc < 0)
    return posix_fail_errno(EBADF);
  return rc;
}

int
chdir(const char *path)
{
  int rc;

  if(path == 0) {
    errno = EINVAL;
    return -1;
  }

  errno = 0;
  rc = __auxv6_sys_chdir(path);
  if(rc < 0)
    return posix_fail_errno(ENOENT);
  return rc;
}

int
dup(int fd)
{
  int rc;

  errno = 0;
  rc = __auxv6_sys_dup(fd);
  if(rc < 0)
    return posix_fail_errno(EBADF);
  return rc;
}

int
dup2(int oldfd, int newfd)
{
  int rc;

  errno = 0;
  rc = __auxv6_sys_dup2(oldfd, newfd);
  if(rc < 0)
    return posix_fail_errno(EBADF);
  return rc;
}

off_t
lseek(int fd, off_t offset, int whence)
{
  off_t rc;

  errno = 0;
  rc = __auxv6_sys_lseek(fd, offset, whence);
  if(rc == (off_t)-1 && errno == 0)
    errno = (whence == SEEK_SET || whence == SEEK_CUR || whence == SEEK_END) ? ESPIPE : EINVAL;
  return rc;
}

int
fcntl(int fd, int cmd, ...)
{
  va_list ap;
  int arg;
  int rc;

  arg = 0;
  va_start(ap, cmd);
  switch(cmd) {
  case 0:
  case 2:
  case 4:
  case 1030:
    arg = va_arg(ap, int);
    break;
  default:
    break;
  }
  va_end(ap);

  errno = 0;
  rc = __auxv6_sys_fcntl(fd, cmd, arg);
  if(rc < 0) {
    if(errno == 0) {
      if(cmd == 0 || cmd == 1030)
        errno = EMFILE;
      else if(cmd < 0 || (cmd > 4 && cmd != 1030))
        errno = EINVAL;
      else
        errno = EBADF;
    }
    return -1;
  }
  return rc;
}

int
ioctl(int fd, int request, ...)
{
  va_list ap;
  void *argp;
  int rc;

  argp = 0;
  va_start(ap, request);
  argp = va_arg(ap, void*);
  va_end(ap);

  errno = 0;
  rc = __auxv6_sys_ioctl(fd, request, argp);
  if(rc < 0)
    return posix_fail_errno(ENOTTY);
  return rc;
}

static void
posix_fill_mode_from_aux(struct stat *dst, const struct auxv6_kstat *src)
{
  mode_t ftype;

  if(dst == 0 || src == 0)
    return;

  if((src->st_mode & S_IFMT) != 0) {
    dst->st_mode = src->st_mode;
    return;
  }

  ftype = 0;
  switch(src->st_type) {
  case AUX_T_FILE:
    ftype = S_IFREG;
    break;
  case AUX_T_DIR:
    ftype = S_IFDIR;
    break;
  case AUX_T_DEV:
    ftype = S_IFCHR;
    break;
  case AUX_T_SYMLINK:
    ftype = S_IFLNK;
    break;
  default:
    break;
  }

  dst->st_mode = (src->st_mode & 07777) | ftype;
}

static void
posix_from_aux_kstat(struct stat *dst, const struct auxv6_kstat *src)
{
  if(dst == 0 || src == 0)
    return;

  memset(dst, 0, sizeof(*dst));

  dst->st_dev = (dev_t)src->st_dev;
  dst->st_ino = (ino_t)src->st_ino;
  dst->st_nlink = (nlink_t)src->st_nlink;
  dst->st_uid = (uid_t)src->st_uid;
  dst->st_gid = (gid_t)src->st_gid;
  dst->st_rdev = makedev((unsigned)src->st_major, (unsigned)src->st_minor);
  dst->st_size = (off_t)src->st_size;
  dst->st_atime = (time_t)src->st_atime;
  dst->st_mtime = (time_t)src->st_mtime;
  dst->st_ctime = (time_t)src->st_ctime;
  dst->st_blksize = 512;
  dst->st_blocks = (blkcnt_t)((src->st_size + 511ULL) / 512ULL);

  posix_fill_mode_from_aux(dst, src);
}

static int
posix_exec_access_mode(const struct stat *st, int mode)
{
  int bits;

  if(st == 0)
    return -1;
  if(mode == F_OK)
    return 0;

  if(geteuid() == 0) {
    if((mode & X_OK) && ((st->st_mode & S_IFMT) != S_IFDIR) &&
       (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0)
      return -1;
    return 0;
  }

  if(geteuid() == st->st_uid)
    bits = (st->st_mode >> 6) & 07;
  else if(getegid() == st->st_gid)
    bits = (st->st_mode >> 3) & 07;
  else
    bits = st->st_mode & 07;

  if((mode & R_OK) && (bits & 04) == 0)
    return -1;
  if((mode & W_OK) && (bits & 02) == 0)
    return -1;
  if((mode & X_OK) && (bits & 01) == 0)
    return -1;
  return 0;
}

int
__posix_stat(const char *path, struct stat *buf)
{
  struct auxv6_kstat kst;
  struct auxv6_kstat lst;

  errno = 0;
  if(__auxv6_sys_stat(path, &kst) < 0) {
    if(errno == 0) {
      if(__auxv6_sys_lstat(path, &lst) == 0 && lst.st_type == AUX_T_SYMLINK)
        errno = ELOOP;
      else
        errno = ENOENT;
    }
    return -1;
  }
  posix_from_aux_kstat(buf, &kst);
  return 0;
}

int
__posix_fstat(int fd, struct stat *buf)
{
  struct auxv6_kstat kst;

  errno = 0;
  if(__auxv6_sys_fstat(fd, &kst) < 0) {
    if(errno == 0)
      errno = EBADF;
    return -1;
  }
  posix_from_aux_kstat(buf, &kst);
  return 0;
}

int
__posix_lstat(const char *path, struct stat *buf)
{
  struct auxv6_kstat kst;

  errno = 0;
  if(__auxv6_sys_lstat(path, &kst) < 0) {
    if(errno == 0)
      errno = ENOENT;
    return -1;
  }
  posix_from_aux_kstat(buf, &kst);
  return 0;
}

int
stat(const char *path, struct stat *buf)
{
  return __posix_stat(path, buf);
}

int
fstat(int fd, struct stat *buf)
{
  return __posix_fstat(fd, buf);
}

int
lstat(const char *path, struct stat *buf)
{
  return __posix_lstat(path, buf);
}

char *
getcwd(char *buf, size_t size)
{
  if(buf == 0 || size == 0) {
    errno = EINVAL;
    return 0;
  }
  errno = 0;
  if(__auxv6_sys_getcwd(buf, size) < 0) {
    if(errno == 0)
      errno = ENOENT;
    return 0;
  }
  return buf;
}

char *
__posix_getcwd(char *buf, size_t size)
{
  return getcwd(buf, size);
}

int
faccessat(int fd, const char *path, int mode, int flag)
{
  struct stat st;

  if(fd != AT_FDCWD) {
    errno = ENOSYS;
    return -1;
  }
  if((flag & ~AT_EACCESS) != 0) {
    errno = EINVAL;
    return -1;
  }
  if((mode & ~(F_OK | R_OK | W_OK | X_OK)) != 0) {
    errno = EINVAL;
    return -1;
  }
  if(__posix_stat(path, &st) < 0)
    return -1;
  if(posix_exec_access_mode(&st, mode) < 0) {
    errno = EACCES;
    return -1;
  }
  return 0;
}

int
access(const char *path, int mode)
{
  return faccessat(AT_FDCWD, path, mode, 0);
}

int
rmdir(const char *path)
{
  struct stat st;
  DIR *dp;
  struct dirent *de;
  int empty;

  if(path == 0 || *path == 0) {
    errno = EINVAL;
    return -1;
  }

  if(strcmp(path, ".") == 0 || strcmp(path, "..") == 0) {
    errno = EINVAL;
    return -1;
  }

  if(__posix_lstat(path, &st) < 0)
    return -1;
  if(!S_ISDIR(st.st_mode)) {
    errno = ENOTDIR;
    return -1;
  }

  dp = opendir(path);
  if(dp == 0) {
    if(errno == 0)
      errno = EACCES;
    return -1;
  }

  empty = 1;
  while((de = readdir(dp)) != 0) {
    if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;
    empty = 0;
    break;
  }
  closedir(dp);

  if(!empty) {
    errno = ENOTEMPTY;
    return -1;
  }

  errno = 0;
  if(__auxv6_sys_rmdir(path) == 0)
    return 0;
  if(errno == 0)
    errno = EIO;
  return -1;
}

int
open64(const char *path, int flags, ...)
{
  return open((char*)path, flags);
}

ssize_t
readlink(const char *path, char *buf, size_t bufsiz)
{
  ssize_t rc;

  errno = 0;
  rc = __auxv6_sys_readlink(path, buf, bufsiz);
  if(rc < 0)
    return posix_fail_errno(ENOENT);
  return rc;
}

int
mkfifo(const char *path, mode_t mode)
{
  return mknod(path, S_IFIFO | (mode & 0777), 0, 0);
}

int
fchmod(int fd, mode_t mode)
{
  errno = 0;
  if(__auxv6_sys_fchmod(fd, mode) < 0)
    return posix_fail_errno(EPERM);
  return 0;
}

int
ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout,
      const sigset_t *sigmask)
{
  int timeout_ms;
  int ret;
  int saved_errno;
  sigset_t oldmask;
  int mask_changed;

  timeout_ms = -1;
  if(timeout != 0) {
    if(timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L) {
      errno = EINVAL;
      return -1;
    }

    /* Clamp to poll(2) int timeout range in milliseconds. */
    if(timeout->tv_sec > 2147483L)
      timeout_ms = 2147483647;
    else
      timeout_ms = (int)(timeout->tv_sec * 1000L + timeout->tv_nsec / 1000000L);
  }

  mask_changed = 0;
  if(sigmask != 0) {
    if(sigprocmask(SIG_SETMASK, sigmask, &oldmask) < 0)
      return -1;
    mask_changed = 1;
  }

  errno = 0;
  ret = poll(fds, nfds, timeout_ms);
  saved_errno = errno;

  if(mask_changed)
    sigprocmask(SIG_SETMASK, &oldmask, 0);

  errno = saved_errno;
  return ret;
}

int
utimensat(int dirfd, const char *path, const struct timespec times[2], int flags)
{
  int rc;

  if(path == 0) {
    errno = EINVAL;
    return -1;
  }

  if((flags & ~AT_SYMLINK_NOFOLLOW) != 0) {
    errno = EINVAL;
    return -1;
  }

  if(times) {
    if(!posix_valid_utimens_nsec(times[0].tv_nsec) ||
       !posix_valid_utimens_nsec(times[1].tv_nsec)) {
      errno = EINVAL;
      return -1;
    }
  }

  errno = 0;
  rc = __auxv6_sys_utimensat(dirfd, path, times, flags);
  if(rc < 0) {
    if(errno == 0) {
      if(dirfd != AT_FDCWD)
        errno = ENOSYS;
      else
        errno = ENOENT;
    }
    return -1;
  }
  return 0;
}

int
futimens(int fd, const struct timespec times[2])
{
  errno = ENOSYS;
  (void)fd;
  (void)times;
  return -1;
}

int
utime(const char *filename, const struct utimbuf *times)
{
  struct timespec ts[2];

  if(filename == 0) {
    errno = EINVAL;
    return -1;
  }

  if(times == 0)
    return utimensat(AT_FDCWD, filename, 0, 0);

  ts[0].tv_sec = times->actime;
  ts[0].tv_nsec = 0;
  ts[1].tv_sec = times->modtime;
  ts[1].tv_nsec = 0;
  return utimensat(AT_FDCWD, filename, ts, 0);
}
