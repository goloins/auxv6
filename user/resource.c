#include "errno.h"
#include "sys/resource.h"
#include "auxv6/user.h"

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