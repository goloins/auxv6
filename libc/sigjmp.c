/*
 * libc/sigjmp.c - POSIX sigsetjmp/siglongjmp wrappers
 */

#include "types.h"
#include "../include/setjmp.h"
#include "../include/signal.h"

int
sigsetjmp(sigjmp_buf env, int savemask)
{
  env[0].__savemask = (savemask != 0);
  if(env[0].__savemask)
    sigprocmask(SIG_SETMASK, 0, &env[0].__mask);
  return setjmp(env[0].__jb);
}

void
siglongjmp(sigjmp_buf env, int val)
{
  if(env[0].__savemask)
    sigprocmask(SIG_SETMASK, &env[0].__mask, 0);
  longjmp(env[0].__jb, val);
}
