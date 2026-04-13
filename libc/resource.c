#include "errno.h"
#include "sys/resource.h"
#include "auxv6/user.h"
#include "string.h"

#define AUXV6_TICKS_PER_SEC 100

int
getrlimit(int resource, struct rlimit *rlp)
{
  if(rlp == 0) {
    errno = EINVAL;
    return -1;
  }

  errno = 0;
  if(__auxv6_sys_getrlimit(resource, rlp) < 0) {
    if(errno == 0)
      errno = EINVAL;
    return -1;
  }

  return 0;
}

int
setrlimit(int resource, const struct rlimit *rlp)
{
  if(rlp == 0) {
    errno = EINVAL;
    return -1;
  }

  errno = 0;
  if(__auxv6_sys_setrlimit(resource, rlp) < 0) {
    if(errno == 0)
      errno = EINVAL;
    return -1;
  }

  return 0;
}

int
getrusage(int who, struct rusage *usage)
{
  int ticks;

  if(usage == 0) {
    errno = EINVAL;
    return -1;
  }
  if(who != RUSAGE_SELF && who != RUSAGE_CHILDREN) {
    errno = EINVAL;
    return -1;
  }

  memset(usage, 0, sizeof(*usage));

  if(who == RUSAGE_CHILDREN)
    return 0;

  ticks = uptime();
  if(ticks < 0) {
    errno = EIO;
    return -1;
  }

  usage->ru_utime.tv_sec = ticks / AUXV6_TICKS_PER_SEC;
  usage->ru_utime.tv_usec = (ticks % AUXV6_TICKS_PER_SEC) * (1000000 / AUXV6_TICKS_PER_SEC);
  return 0;
}