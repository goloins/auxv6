/*
 * posix_fs.c - path and filesystem wrappers split out of user/posix.c
 */

#include "types.h"
#include "fcntl.h"
#include "stat.h"
#include "errno.h"
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
  if(stat(path, buf) < 0) {
    if(errno == 0) {
      if(lstat(path, &lst) == 0 && lst.st_type == T_SYMLINK)
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
  if(fstat(fd, buf) < 0) {
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
  if(lstat(path, buf) < 0) {
    if(errno == 0)
      errno = ENOENT;
    return -1;
  }
  posix_fixup_mode_from_type(buf);
  return 0;
}

char *
__posix_getcwd(char *buf, size_t size)
{
  if(buf == 0 || size == 0) {
    errno = EINVAL;
    return 0;
  }
  errno = 0;
  if(getcwd(buf, (int)size) < 0) {
    if(errno == 0)
      errno = ENOENT;
    return 0;
  }
  return buf;
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
open64(const char *path, int flags, ...)
{
  return open((char*)path, flags);
}