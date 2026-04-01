/* config.h — dash 0.5.12 for auxv6
 *
 * Generated manually for auxv6 (xv6-derived OS, x86-32).
 * Do not run autoconf on this file.
 */

#ifndef _CONFIG_H
#define _CONFIG_H

/* --- Package identity ---------------------------------------------------- */
#define PACKAGE         "dash"
#define PACKAGE_NAME    "dash"
#define PACKAGE_VERSION "0.5.12"
#define VERSION         "0.5.12"

/* --- Feature: __attribute__((__alias__())) -------------------------------- */
#define HAVE_ALIAS_ATTRIBUTE 1

/* --- Headers we provide via include/posix/ -------------------------------- */
#define HAVE_INTTYPES_H   1
#define HAVE_STDINT_H     1
#define HAVE_STDLIB_H     1
#define HAVE_STRING_H     1
#define HAVE_STRINGS_H    1    /* provided by string.h / stdlib.h */
#define HAVE_SYS_STAT_H   1
#define HAVE_SYS_TYPES_H  1
#define HAVE_UNISTD_H     1
#define HAVE_PATHS_H      1
/* #undef HAVE_MEMORY_H */      /* no <memory.h> */
#define HAVE_ALLOCA_H     1    /* posix/alloca.h provides __builtin_alloca */

/* --- Functions available in auxv6 / posix.c ------------------------------ */
#define HAVE_ISALPHA      1    /* provided by posix/ctype.h */
#define HAVE_DECL_ISBLANK 1    /* provided by posix/ctype.h */
#define HAVE_BSEARCH      1    /* provided by stdlib.h */
#define HAVE_STPCPY       1    /* provided by string.h */
#define HAVE_MEMPCPY      1    /* provided by string.h */
#define HAVE_STRSIGNAL    1    /* provided by string.h */
/* #undef HAVE_STRTOD */        /* no float — system.h stubs it */
#define HAVE_STRTOIMAX    1    /* defined as strtoll in inttypes.h */
#define HAVE_STRTOUMAX    1    /* defined as strtoull in inttypes.h */
#define HAVE_SYSCONF      1    /* stubbed in system.h / by POSIX layer */
#define HAVE_KILLPG       1    /* inline in system.h via kill(-pid,sig) */

/* --- Functions we do NOT have -------------------------------------------- */
/* #undef HAVE_FACCESSAT */
/* #undef HAVE_TRADITIONAL_FACCESSAT */
/* #undef HAVE_FNMATCH */            /* no glob pattern matching */
/* #undef HAVE_GLOB */               /* no glob() */
/* #undef HAVE_GETPWNAM */           /* no passwd database */
/* #undef HAVE_GETRLIMIT */          /* stubs in posix/sys/resource.h */
/* #undef HAVE_SIGSETMASK */         /* use sigprocmask instead */

/* struct stat — auxv6 uses st_mtim? No. */
/* #undef HAVE_ST_MTIM */            /* auxv6 stat has no st_mtim */

/* --- Paths ---------------------------------------------------------------- */
#define _PATH_BSHELL    "/bin/sh"
#define _PATH_DEVNULL   "/dev/null"
#define _PATH_TTY       "/dev/tty"

/* --- dash build options --------------------------------------------------- */

/* JOBS: enable job control (we have kill, setpgid, tcsetpgrp etc.) */
#define JOBS 1

/* SMALL: disable history and editline (saves code size, no libedit) */
#define SMALL 1

/* No line number tracking in error messages (saves memory) */
/* #define WITH_LINENO */

/* Do not use glibc stdio FILE* for dash's internal output buffers */
/* #define USE_GLIBC_STDIO */

/* Disable the DEBUG tracing subsystem */
/* #define DEBUG */

/* --- C11 / POSIX compat --------------------------------------------------- */
#define _POSIX_SOURCE    1
#define BSD              1
#define SHELL            1

#endif /* _CONFIG_H */
