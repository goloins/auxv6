#ifndef XV6_SIGNAL_H
#define XV6_SIGNAL_H

#include "types.h"

// Minimal POSIX-like signal numbering and APIs.
#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGKILL  9
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20

#define NSIG             32
#define SIGBIT(sig)      (1U << ((sig) - 1))

typedef uint sigset_t;

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SIG_DFL ((void(*)(int))0)
#define SIG_IGN ((void(*)(int))1)

struct sigaction {
  void (*sa_handler)(int);
  sigset_t sa_mask;
  int sa_flags;
};

#endif