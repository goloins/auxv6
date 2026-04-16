/*
 * libc/sigjmp.c - POSIX sigsetjmp/siglongjmp wrappers
 */

#include "../include/setjmp.h"
#include "../include/signal.h"

void
siglongjmp(sigjmp_buf env, int val)
{
  if(env[0].__savemask)
    sigprocmask(SIG_SETMASK, &env[0].__mask, 0);
  longjmp(env[0].__jb, val);
}
