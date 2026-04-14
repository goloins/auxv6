/*
 * include/sys/wait.h — thin wrapper for auxv6's include/wait.h
 *
 * Placed here so that posix/sys/wait.h can reach WNOHANG etc.
 * via #include_next <sys/wait.h>.
 */
#ifndef _SYS_WAIT_NATIVE_H
#define _SYS_WAIT_NATIVE_H

#include "../wait.h"

#include "types.h"

int wait(int *status);
int waitpid(int pid, int *status, int options);

#endif /* _SYS_WAIT_NATIVE_H */
