/*
 * <sys/un.h> - UNIX domain socket address definitions
 *
 * This provides the POSIX sockaddr_un type used by software that references
 * AF_UNIX paths. auxv6 does not yet implement the full UNIX-domain ancillary
 * messaging surface (sendmsg/recvmsg, SCM_RIGHTS, etc.).
 */

#ifndef _SYS_UN_H
#define _SYS_UN_H

#include "sys/socket.h"

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

struct sockaddr_un {
  sa_family_t sun_family;
  char sun_path[UNIX_PATH_MAX];
};

#ifndef SUN_LEN
#define SUN_LEN(su) ((socklen_t)(sizeof((su)->sun_family) + __builtin_strlen((su)->sun_path)))
#endif

#endif /* _SYS_UN_H */
