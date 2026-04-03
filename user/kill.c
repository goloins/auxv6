#include "types.h"
#include "stat.h"
#include "signal.h"
#include "string.h"
#include "stdlib.h"
#include "auxv6/user.h"

/*
 * Signal name table — indexed by signal number.
 * Follows POSIX / Linux x86 numbering (matches signal.h).
 */
static const char *sig_names[NSIG] = {
  /*  0 */ 0,
  /*  1 */ "HUP",
  /*  2 */ "INT",
  /*  3 */ "QUIT",
  /*  4 */ "ILL",
  /*  5 */ "TRAP",
  /*  6 */ "ABRT",
  /*  7 */ "BUS",
  /*  8 */ "FPE",
  /*  9 */ "KILL",
  /* 10 */ "USR1",
  /* 11 */ "SEGV",
  /* 12 */ "USR2",
  /* 13 */ "PIPE",
  /* 14 */ "ALRM",
  /* 15 */ "TERM",
  /* 16 */ "STKFLT",
  /* 17 */ "CHLD",
  /* 18 */ "CONT",
  /* 19 */ "STOP",
  /* 20 */ "TSTP",
  /* 21 */ "TTIN",
  /* 22 */ "TTOU",
  /* 23 */ "URG",
  /* 24 */ "XCPU",
  /* 25 */ "XFSZ",
  /* 26 */ "VTALRM",
  /* 27 */ "PROF",
  /* 28 */ "WINCH",
  /* 29 */ "IO",
  /* 30 */ "PWR",
  /* 31 */ "SYS",
};

static int
sig_by_name(const char *name)
{
  int i;
  const char *n;

  /* Strip leading "SIG" prefix if present */
  if(strncmp(name, "SIG", 3) == 0)
    name += 3;

  for(i = 1; i < NSIG; i++) {
    n = sig_names[i];
    if(n == 0)
      continue;
    if(strcmp(n, name) == 0)
      return i;
  }
  return -1;
}

/*
 * Parse a signal specifier: a number, a name ("TERM"), or "SIG"-prefixed
 * name ("SIGTERM").  Returns the signal number or -1 on failure.
 */
static int
parse_signal(const char *s)
{
  int sig;

  /* Pure number */
  if(*s >= '0' && *s <= '9') {
    sig = atoi(s);
    if(sig > 0 && sig < NSIG)
      return sig;
    return -1;
  }

  return sig_by_name(s);
}

static void
list_signals(void)
{
  int i;

  for(i = 1; i < NSIG; i++) {
    if(sig_names[i] == 0)
      continue;
    dprintf(1, "%2d) SIG%s\n", i, sig_names[i]);
  }
}

static void
usage(void)
{
  dprintf(2, "usage: kill [-l] [-s signal] [-signal] pid...\n");
  exit(1);
}

int
main(int argc, char **argv)
{
  int sig;
  int i;
  int firstpid;

  sig      = SIGTERM;
  firstpid = 1;

  if(argc < 2)
    usage();

  /* -l: list signals */
  if(strcmp(argv[1], "-l") == 0) {
    list_signals();
    exit(0);
  }

  /* -s signal_name pid... */
  if(strcmp(argv[1], "-s") == 0) {
    if(argc < 4)
      usage();
    sig = parse_signal(argv[2]);
    if(sig < 0) {
      dprintf(2, "kill: unknown signal '%s'\n", argv[2]);
      exit(1);
    }
    firstpid = 3;
  } else if(argv[1][0] == '-' && argv[1][1] != '\0') {
    /* -<number> or -<name> or -SIG<name> */
    sig = parse_signal(argv[1] + 1);
    if(sig < 0) {
      dprintf(2, "kill: unknown signal '%s'\n", argv[1] + 1);
      exit(1);
    }
    firstpid = 2;
  }

  if(firstpid >= argc)
    usage();

  for(i = firstpid; i < argc; i++) {
    int pid;

    pid = atoi(argv[i]);
    if(pid <= 0) {
      dprintf(2, "kill: invalid pid '%s'\n", argv[i]);
      continue;
    }
    sigsend(pid, sig);
  }

  exit(0);
}

