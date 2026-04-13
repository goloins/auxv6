#include "types.h"
#include "defs.h"
#include "param.h"
#include "proc.h"
#include "signal.h"

int
valid_signo(int signo)
{
  return signo > 0 && signo < NSIG;
}

int
signal_pick_stop(uint pending)
{
  if(pending & SIGBIT(SIGSTOP))
    return SIGSTOP;
  if(pending & SIGBIT(SIGTSTP))
    return SIGTSTP;
  if(pending & SIGBIT(SIGTTIN))
    return SIGTTIN;
  if(pending & SIGBIT(SIGTTOU))
    return SIGTTOU;
  return 0;
}

int
signal_pick_fatal(uint pending)
{
  // Check in priority order
  if(pending & SIGBIT(SIGKILL))
    return SIGKILL;
  if(pending & SIGBIT(SIGSEGV))
    return SIGSEGV;
  if(pending & SIGBIT(SIGBUS))
    return SIGBUS;
  if(pending & SIGBIT(SIGILL))
    return SIGILL;
  if(pending & SIGBIT(SIGFPE))
    return SIGFPE;
  if(pending & SIGBIT(SIGABRT))
    return SIGABRT;
  if(pending & SIGBIT(SIGTERM))
    return SIGTERM;
  if(pending & SIGBIT(SIGINT))
    return SIGINT;
  if(pending & SIGBIT(SIGQUIT))
    return SIGQUIT;
  if(pending & SIGBIT(SIGHUP))
    return SIGHUP;
  if(pending & SIGBIT(SIGTRAP))
    return SIGTRAP;
  if(pending & SIGBIT(SIGPIPE))
    return SIGPIPE;
  if(pending & SIGBIT(SIGALRM))
    return SIGALRM;
  if(pending & SIGBIT(SIGUSR1))
    return SIGUSR1;
  if(pending & SIGBIT(SIGUSR2))
    return SIGUSR2;
  if(pending & SIGBIT(SIGXCPU))
    return SIGXCPU;
  if(pending & SIGBIT(SIGXFSZ))
    return SIGXFSZ;
  if(pending & SIGBIT(SIGSYS))
    return SIGSYS;
  return 0;
}

void
proc_note_signal_locked(struct proc *p, int signo)
{
  if(p == 0 || !valid_signo(signo))
    return;

  p->sig_pending |= SIGBIT(signo);

  if(signo == SIGCONT) {
    // Continue clears pending stop intents and resumes stopped tasks.
    p->sig_pending &= ~(SIGBIT(SIGSTOP) | SIGBIT(SIGTSTP));
    if(p->state == STOPPED)
      p->state = RUNNABLE;
  }

  // Preserve existing semantics: SIGKILL marks process as killed.
  if(signo == SIGKILL)
    p->killed = 1;

  // Signals should wake a sleeping process so delivery can progress.
  if(p->state == SLEEPING)
    p->state = RUNNABLE;
}