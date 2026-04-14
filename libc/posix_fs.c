/*
 * posix_fs.c - path and filesystem wrappers split out of user/posix.c
 */

#include "types.h"
#include "fcntl.h"
#include "stat.h"
#include "dirent.h"
#include "errno.h"
#include "stdarg.h"
#include "string.h"
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

int __auxv6_sys_open(const char *path, int flags);
ssize_t __auxv6_sys_read(int fd, void *buf, size_t count);
ssize_t __auxv6_sys_write(int fd, const void *buf, size_t count);
int __auxv6_sys_close(int fd);
int __auxv6_sys_fstat(int fd, struct stat *st);
int __auxv6_sys_stat(const char *path, struct stat *st);
int __auxv6_sys_lstat(const char *path, struct stat *st);
int __auxv6_sys_chdir(const char *path);
int __auxv6_sys_dup(int fd);
int __auxv6_sys_dup2(int oldfd, int newfd);
off_t __auxv6_sys_lseek(int fd, off_t offset, int whence);
int __auxv6_sys_fcntl(int fd, int cmd, int arg);
int __auxv6_sys_ioctl(int fd, int request, void *argp);
ssize_t __auxv6_sys_readlink(const char *path, char *buf, size_t bufsiz);

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
posix_fixup_mode_from_type(struct stat *st)
{
  int ftype;

  if(st == 0)
    return;
  if((st->st_mode & M_IFMT) != 0)
    return;

  ftype = 0;
  switch(st->st_type) {
  case T_FILE:
    ftype = M_IFREG;
    break;
  case T_DIR:
    ftype = M_IFDIR;
    break;
  case T_DEV:
    ftype = M_IFCHR;
    break;
  default:
    break;
  }
  st->st_mode = (st->st_mode & 07777) | ftype;
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
    if((mode & X_OK) && ((st->st_mode & M_IFMT) != M_IFDIR) &&
       (st->st_mode & (M_IXUSR | M_IXGRP | M_IXOTH)) == 0)
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
  struct stat lst;

  errno = 0;
  if(__auxv6_sys_stat(path, buf) < 0) {
    if(errno == 0) {
      if(__auxv6_sys_lstat(path, &lst) == 0 && lst.st_type == T_SYMLINK)
        errno = ELOOP;
      else
        errno = ENOENT;
    }
    return -1;
  }
  posix_fixup_mode_from_type(buf);
  return 0;
}

int
__posix_fstat(int fd, struct stat *buf)
{
  errno = 0;
  if(__auxv6_sys_fstat(fd, buf) < 0) {
    if(errno == 0)
      errno = EBADF;
    return -1;
  }
  posix_fixup_mode_from_type(buf);
  return 0;
}

int
__posix_lstat(const char *path, struct stat *buf)
{
  errno = 0;
  if(__auxv6_sys_lstat(path, buf) < 0) {
    if(errno == 0)
      errno = ENOENT;
    return -1;
  }
  posix_fixup_mode_from_type(buf);
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
  if(st.st_type != T_DIR) {
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
  return mknod(path, M_IFIFO | (mode & 0777), 0, 0);
}

int
fchmod(int fd, mode_t mode)
{
  /* auxv6 has no fchmod syscall; no-op returning success. */
  (void)fd;
  (void)mode;
  return 0;
}
