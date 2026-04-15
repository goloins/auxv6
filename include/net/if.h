/*
 * <net/if.h> - network interface definitions
 */

#ifndef _NET_IF_H_
#define _NET_IF_H_

#include "../sys/ioctl.h"
#include "../net.h"

/* Socket ioctl request codes used by OpenSSH portability paths. */
#ifndef SIOCGIFFLAGS
#define SIOCGIFFLAGS 0x8913
#endif
#ifndef SIOCSIFFLAGS
#define SIOCSIFFLAGS 0x8914
#endif

#endif /* _NET_IF_H_ */