#ifndef _ARPA_INET_H_
#define _ARPA_INET_H_

/*
 * arpa/inet.h – inet address conversion declarations.
 * Definitions live in user/ulib.c.
 */

#include "../netinet/in.h"

/* Byte-order helpers (re-exported for convenience). */
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

/*
 * inet_aton – Convert a dotted-quad string into a binary address.
 * Returns 1 on success, 0 on error.  Stores result in *ap if non-NULL.
 */
int inet_aton(const char *cp, struct in_addr *ap);

/*
 * inet_addr – Convert a dotted-quad string, returning the binary
 * address in network byte order.  Returns INADDR_NONE on error.
 */
uint inet_addr(const char *cp);

/*
 * inet_ntoa – Convert a binary address to dotted-quad notation.
 * Returns a pointer to a static buffer (not re-entrant).
 */
char *inet_ntoa(struct in_addr in);

/*
 * inet_pton – Presentation-to-network conversion.
 * Only AF_INET is supported.  Returns 1 on success, 0 if src is
 * not a valid address in the given family, -1 on unsupported family.
 */
int inet_pton(int af, const char *src, void *dst);

/*
 * inet_ntop – Network-to-presentation conversion.
 * Only AF_INET is supported.  Returns dst on success, 0 on error.
 */
const char *inet_ntop(int af, const void *src, char *dst, uint size);

#endif /* _ARPA_INET_H_ */
