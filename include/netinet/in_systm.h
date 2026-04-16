/*
 * <netinet/in_systm.h> - legacy Internet subsystem typedefs
 *
 * OpenSSH/openbsd-compat includes this header for historical BSD typedefs.
 */

#ifndef _NETINET_IN_SYSTM_H
#define _NETINET_IN_SYSTM_H

#include "sys/types.h"

typedef u_short n_short; /* short as received from the net */
typedef u_long  n_long;  /* long as received from the net */
typedef u_long  n_time;  /* ms since 00:00 GMT, byte rev */

#endif /* _NETINET_IN_SYSTM_H */