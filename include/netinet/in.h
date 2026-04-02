#ifndef _NETINET_IN_H_
#define _NETINET_IN_H_

/*
 * netinet/in.h – POSIX-compatible internet address family definitions.
 * Re-exports the core socket types from include/socket.h so that userspace
 * programs can #include <netinet/in.h> in the conventional POSIX style.
 */

#include "../socket.h"   /* sockaddr_in, AF_INET, INADDR_*, IPPROTO_* */
#include "../net.h"      /* net_htons / net_htonl helpers */

/* Opaque address type used by inet_*() helpers. */
struct in_addr {
  uint s_addr;
};

/* Byte-order conversion macros. */
#ifndef htons
#define htons(x)  net_htons((ushort)(x))
#endif
#ifndef htonl
#define htonl(x)  net_htonl((uint)(x))
#endif
#ifndef ntohs
#define ntohs(x)  net_ntohs((ushort)(x))
#endif
#ifndef ntohl
#define ntohl(x)  net_ntohl((uint)(x))
#endif

/* Additional well-known addresses. */
#ifndef INADDR_BROADCAST
#define INADDR_BROADCAST  0xffffffffU   /* 255.255.255.255 */
#endif
#ifndef INADDR_NONE
#define INADDR_NONE       0xffffffffU   /* error sentinel for inet_addr() */
#endif

/* Well-known port boundary. */
#define IPPORT_RESERVED   1024

/* Address-class predicates (host-byte-order input). */
#define IN_CLASSA(a)  (((uint)(a) & 0x80000000U) == 0)
#define IN_CLASSB(a)  (((uint)(a) & 0xc0000000U) == 0x80000000U)
#define IN_CLASSC(a)  (((uint)(a) & 0xe0000000U) == 0xc0000000U)
#define IN_CLASSD(a)  (((uint)(a) & 0xf0000000U) == 0xe0000000U)

#endif /* _NETINET_IN_H_ */
