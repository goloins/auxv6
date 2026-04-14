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

/*
 * sigsetjmp must be a macro so setjmp captures the caller's frame.
 * A function wrapper would save the wrong context and make siglongjmp unsafe.
 */
#define sigsetjmp(env, savemask) \
	( ((env)[0].__savemask = ((savemask) != 0)), \
		((env)[0].__savemask ? sigprocmask(SIG_SETMASK, (const sigset_t *)0, &((env)[0].__mask)) : 0), \
		setjmp((env)[0].__jb) )

void siglongjmp(sigjmp_buf env, int val) __attribute__((noreturn));

#endif /* _SETJMP_H */