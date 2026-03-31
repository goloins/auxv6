/*
 * posix/alloca.h — alloca for auxv6
 *
 * __builtin_alloca is a GCC intrinsic; it works even with -fno-builtin
 * because that flag only suppresses standard C library functions.
 */
#ifndef _ALLOCA_H
#define _ALLOCA_H

#define alloca(size)    __builtin_alloca(size)

#endif /* _ALLOCA_H */
