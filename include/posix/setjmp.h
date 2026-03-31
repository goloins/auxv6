/*
 * posix/setjmp.h — setjmp/longjmp for auxv6 (i386)
 *
 * jmp_buf layout: [ebx, esi, edi, ebp, esp, eip]
 */
#ifndef _SETJMP_H
#define _SETJMP_H

typedef int jmp_buf[6];

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif /* _SETJMP_H */
