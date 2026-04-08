/*
 * <stddef.h> - Standard Type Definitions
 *
 * POSIX.1-2017 / C11 compatible definitions
 */

#ifndef _STDDEF_H
#define _STDDEF_H

/* NULL pointer constant */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* size_t - Unsigned integer type for sizeof */
#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned int size_t;
#endif

/* ssize_t - Signed size type (POSIX extension) */
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef int ssize_t;
#endif

/* ptrdiff_t - Signed integer type for pointer subtraction */
#ifndef _PTRDIFF_T_DEFINED
#define _PTRDIFF_T_DEFINED
typedef int ptrdiff_t;
#endif

/* wchar_t - Wide character type */
#ifndef _WCHAR_T_DEFINED
#define _WCHAR_T_DEFINED
#ifdef __WCHAR_TYPE__
typedef __WCHAR_TYPE__ wchar_t;
#else
typedef int wchar_t;
#endif
#endif

/* offsetof - Byte offset of member within struct */
#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif

/* max_align_t - Type with maximum alignment requirement */
typedef long double max_align_t;

#endif /* _STDDEF_H */
