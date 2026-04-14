/*
 * <setjmp.h> - setjmp/longjmp for auxv6 (i386)
 */

#ifndef _SETJMP_H
#define _SETJMP_H

#include "signal.h"

typedef int jmp_buf[6];

/*
 * POSIX signal-aware jump buffer.
 * When sigsetjmp(env, 1) is used, siglongjmp restores the saved signal mask.
 */
typedef struct {
	jmp_buf  __jb;
	sigset_t __mask;
	int      __savemask;
} sigjmp_buf[1];

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

int  sigsetjmp(sigjmp_buf env, int savemask);
void siglongjmp(sigjmp_buf env, int val) __attribute__((noreturn));

#endif /* _SETJMP_H */