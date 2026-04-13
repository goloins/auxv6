/*
 * conf.c - configuration, identity, and small POSIX helper wrappers
 */

#include "types.h"
#include "param.h"
#include "stat.h"
#include "errno.h"
#include "limits.h"
#include "unistd.h"
#include "stdlib.h"
#include "sys/time.h"
#include "auxv6/user.h"

static const char auxv6_default_path[] = "/:/bin:/sbin:/usr/bin:/usr/sbin:/usr/share/games";
static const char auxv6_default_login[] = "root";
static const char auxv6_hostname[] = "auxv6";

static int
conf_validate_path(const char *path)
{
  struct stat st;

  if(path == 0) {
    errno = EINVAL;
    return -1;
  }

  errno = 0;
  if(stat(path, &st) < 0 && lstat(path, &st) < 0) {
    if(errno == 0)
      errno = ENOENT;
    return -1;
  }

  return 0;
}

static int
conf_validate_fd(int fd)
{
  struct stat st;

  errno = 0;
  if(fstat(fd, &st) < 0) {
    if(errno == 0)
      errno = EBADF;
    return -1;
  }

  return 0;
}

static long
conf_pathconf_value(int name, int *known)
{
  *known = 1;

  switch(name) {
  case _PC_LINK_MAX:
    return 127;
  case _PC_MAX_CANON:
    return 255;
  case _PC_MAX_INPUT:
    return 255;
  case _PC_NAME_MAX:
    return NAME_MAX;
  case _PC_PATH_MAX:
    return PATH_MAX;
  case _PC_PIPE_BUF:
    return 512;
  case _PC_CHOWN_RESTRICTED:
    return 1;
  case _PC_NO_TRUNC:
    return 1;
  case _PC_VDISABLE:
    return 0;
  case _PC_SYNC_IO:
    return 0;
  case _PC_ASYNC_IO:
    return 0;
  case _PC_PRIO_IO:
    return 0;
  case _PC_FILESIZEBITS:
    return 32;
  default:
    *known = 0;
    return -1;
  }
}

static size_t
conf_copy_value(char *buf, size_t len, const char *value)
{
  size_t need;
  size_t n;

  need = strlen(value) + 1;
  if(buf == 0 || len == 0)
    return need;

  n = need;
  if(n > len)
    n = len;

  memmove(buf, value, n);
  if(n == len)
    buf[len - 1] = '\0';

  return need;
}

static const char *
conf_login_name(void)
{
  char *name;

  name = getenv("LOGNAME");
  if(name && *name)
    return name;

  name = getenv("USER");
  if(name && *name)
    return name;

  return auxv6_default_login;
}

long
sysconf(int name)
{
  switch(name) {
  case _SC_ARG_MAX:
    return ARG_MAX;
  case _SC_CHILD_MAX:
    return CHILD_MAX;
  case _SC_CLK_TCK:
    return 100;
  case _SC_NGROUPS_MAX:
    return NGROUPS_MAX;
  case _SC_OPEN_MAX:
    return NOFILE;
  case _SC_STREAM_MAX:
    return NOFILE;
  case _SC_TZNAME_MAX:
    return 3;
  case _SC_JOB_CONTROL:
    return 1;
  case _SC_SAVED_IDS:
    return 0;
  case _SC_VERSION:
    return _POSIX_VERSION;
  case _SC_PAGESIZE:
#if _SC_PAGE_SIZE != _SC_PAGESIZE
  case _SC_PAGE_SIZE:
#endif
    return 4096;
  case _SC_NPROCESSORS_CONF:
  case _SC_NPROCESSORS_ONLN:
    return 1;
  case _SC_PHYS_PAGES:
  case _SC_AVPHYS_PAGES:
    return 0;
  default:
    errno = EINVAL;
    return -1;
  }
}

long
pathconf(const char *path, int name)
{
  int known;
  long value;

  if(conf_validate_path(path) < 0)
    return -1;

  errno = 0;
  value = conf_pathconf_value(name, &known);
  if(!known) {
    errno = EINVAL;
    return -1;
  }

  return value;
}

long
fpathconf(int fd, int name)
{
  int known;
  long value;

  if(conf_validate_fd(fd) < 0)
    return -1;

  errno = 0;
  value = conf_pathconf_value(name, &known);
  if(!known) {
    errno = EINVAL;
    return -1;
  }

  return value;
}

size_t
confstr(int name, char *buf, size_t len)
{
  switch(name) {
  case _CS_PATH:
    return conf_copy_value(buf, len, auxv6_default_path);
  case _CS_POSIX_V7_ILP32_OFF32_CFLAGS:
    return conf_copy_value(buf, len, "-m32");
  case _CS_POSIX_V7_ILP32_OFF32_LDFLAGS:
    return conf_copy_value(buf, len, "");
  case _CS_POSIX_V7_ILP32_OFF32_LIBS:
    return conf_copy_value(buf, len, "");
  default:
    errno = EINVAL;
    return 0;
  }
}

char*
getlogin(void)
{
  static char name[32];

  if(getlogin_r(name, sizeof(name)) != 0)
    return 0;
  return name;
}

int
getlogin_r(char *buf, size_t bufsize)
{
  const char *name;
  size_t need;

  if(buf == 0 || bufsize == 0)
    return EINVAL;

  name = conf_login_name();
  need = strlen(name) + 1;
  if(bufsize < need)
    return ERANGE;

  memmove(buf, name, need);
  return 0;
}

int
usleep(useconds_t usec)
{
  struct timeval tv;

  if(usec == 0)
    return 0;

  tv.tv_sec = usec / 1000000U;
  tv.tv_usec = usec % 1000000U;

  if(select(0, 0, 0, 0, &tv) < 0) {
    if(errno == 0)
      errno = EINTR;
    return -1;
  }

  return 0;
}

int
pause(void)
{
  if(select(0, 0, 0, 0, 0) < 0) {
    if(errno == 0)
      errno = EINTR;
    return -1;
  }

  errno = EINTR;
  return -1;
}

int
gethostname(char *name, size_t len)
{
  size_t need;

  if(name == 0 || len == 0) {
    errno = EINVAL;
    return -1;
  }

  need = sizeof(auxv6_hostname);
  if(len < need) {
    memmove(name, auxv6_hostname, len);
    name[len - 1] = '\0';
    errno = ENAMETOOLONG;
    return -1;
  }

  memmove(name, auxv6_hostname, need);
  return 0;
}

int
sethostname(const char *name, size_t len)
{
  (void)name;
  (void)len;
  errno = ENOSYS;
  return -1;
}

int
fsync(int fd)
{
  if(conf_validate_fd(fd) < 0)
    return -1;
  return 0;
}

int
fdatasync(int fd)
{
  return fsync(fd);
}

void
sync(void)
{
}

int
nice(int inc)
{
  (void)inc;
  return 0;
}

unsigned int
swab(const void *from, void *to, ssize_t n)
{
  const uchar *src;
  uchar *dst;
  unsigned int copied;

  src = (const uchar*)from;
  dst = (uchar*)to;
  copied = 0;
  while(n > 1) {
    dst[0] = src[1];
    dst[1] = src[0];
    src += 2;
    dst += 2;
    n -= 2;
    copied += 2;
  }

  return copied;
}

int
chroot(const char *path)
{
  if(path == 0) {
    errno = EINVAL;
    return -1;
  }

  errno = ENOSYS;
  return -1;
}