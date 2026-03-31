/*
 * <limits.h> - implementation-defined constants
 */

#ifndef _LIMITS_H
#define _LIMITS_H

#include <stdint.h>

/* Path/name lengths */
#define PATH_MAX     256
#define NAME_MAX     255
#define MAXPATHLEN   PATH_MAX    /* BSD compat */

/* Filesystem limits */
#define NGROUPS_MAX  8
#define ARG_MAX      4096
#define OPEN_MAX     64          /* same as NOFILE in param.h */
#define CHILD_MAX    64

/* Character type */
#define CHAR_BIT     8
#define CHAR_MIN     (-128)
#define CHAR_MAX     127
#define UCHAR_MAX    255
#define SCHAR_MIN    (-128)
#define SCHAR_MAX    127

/* Short */
#define SHRT_MIN     (-32768)
#define SHRT_MAX     32767
#define USHRT_MAX    65535

/* Int */
#define INT_MIN      (-2147483648)
#define INT_MAX      2147483647
#define UINT_MAX     4294967295U

/* Long (32-bit) */
#define LONG_MIN     (-2147483648L)
#define LONG_MAX     2147483647L
#define ULONG_MAX    4294967295UL

/* Long long */
#define LLONG_MIN    (-9223372036854775808LL)
#define LLONG_MAX    9223372036854775807LL
#define ULLONG_MAX   18446744073709551615ULL

/* POSIX specific */
#define SSIZE_MAX    INT_MAX
#define SIZE_MAX     UINT_MAX

/* Symlink (no symlinks in auxv6, but declare for headers that check) */
#define SYMLINK_MAX  0
#define MAXSYMLINKS  0

#endif /* _LIMITS_H */
