/*
 * <sys/utsname.h> - system identification
 */

#ifndef _SYS_UTSNAME_H_
#define _SYS_UTSNAME_H_

#define _UTSNAME_LENGTH 65

struct utsname {
  char sysname[_UTSNAME_LENGTH];
  char nodename[_UTSNAME_LENGTH];
  char release[_UTSNAME_LENGTH];
  char version[_UTSNAME_LENGTH];
  char machine[_UTSNAME_LENGTH];
};

int __auxv6_posix_uname(struct utsname *name);

#ifndef AUXV6_DISABLE_POSIX_UNAME_MACRO
#define uname(name) __auxv6_posix_uname(name)
#endif

#endif /* _SYS_UTSNAME_H_ */