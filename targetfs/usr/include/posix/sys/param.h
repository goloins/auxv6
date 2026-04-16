/*
 * <sys/param.h> - system parameters
 *
 * Provides common BSD/POSIX constants expected by ported software.
 */

#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

#include "sys/types.h"

/* BSD version tag used by some ports to detect BSD-ish environment */
#define BSD     1
#define BSD4_4  1

/* Path and name limits */
#ifndef MAXPATHLEN
#define MAXPATHLEN  256
#endif
#ifndef MAXNAMLEN
#define MAXNAMLEN   255
#endif

/* I/O sizes */
#ifndef PIPE_BUF
/* POSIX minimum atomic pipe write size. Kernel pipe capacity may be larger. */
#define PIPE_BUF    512
#endif
#ifndef NOFILE
#define NOFILE      64
#endif

/* Page size */
#ifndef NBPG
#define NBPG        4096
#endif

/* MIN / MAX helpers */
#ifndef MIN
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#endif

/* Rounding macros */
#define roundup(x, y)  ((((x) + ((y) - 1)) / (y)) * (y))
#define howmany(x, y)  (((x) + ((y) - 1)) / (y))

#endif /* _SYS_PARAM_H */
