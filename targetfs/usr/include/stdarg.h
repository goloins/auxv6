/*
 * <stdarg.h> - variable argument lists (GCC builtin implementation)
 *
 * Uses compiler intrinsics, which are always available regardless of
 * -fno-builtin.
 */

#ifndef AUXV6_STDARG_H
#define AUXV6_STDARG_H

typedef __builtin_va_list va_list;

#define va_start(ap, last)  __builtin_va_start((ap), (last))
#define va_arg(ap, type)    __builtin_va_arg((ap), type)
#define va_end(ap)          __builtin_va_end(ap)
#define va_copy(d, s)       __builtin_va_copy((d), (s))

#ifndef __GNUC_VA_LIST
#define __GNUC_VA_LIST 1
typedef __builtin_va_list __gnuc_va_list;
#endif

#endif /* AUXV6_STDARG_H */