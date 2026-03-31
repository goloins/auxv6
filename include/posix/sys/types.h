/*
 * <sys/types.h> - POSIX data types
 *
 * Wraps auxv6's sys/types.h directly.  Provided here so that ported
 * software that includes <sys/types.h> finds it in the posix/ search path.
 */

#ifndef _POSIX_SYS_TYPES_H
#define _POSIX_SYS_TYPES_H

#include "sys/types.h"

/* ssize_t is signed counterpart of size_t */
#ifndef _SSIZE_T
#define _SSIZE_T
typedef int ssize_t;
#endif

#endif /* _POSIX_SYS_TYPES_H */
