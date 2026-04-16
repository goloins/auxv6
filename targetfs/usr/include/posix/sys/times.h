/*
 * posix/sys/times.h — process CPU times (POSIX.1)
 *
 * auxv6 has no times() syscall; we provide a zero-returning stub so
 * that dash's built-in 'times' command links cleanly.
 */
#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H

#include <sys/types.h>

/* clock_t is defined in sys/types.h as int */

struct tms {
    clock_t tms_utime;     /* user CPU time */
    clock_t tms_stime;     /* system CPU time */
    clock_t tms_cutime;    /* user CPU time of waited-for children */
    clock_t tms_cstime;    /* system CPU time of waited-for children */
};

/* Stub: auxv6 does not track process CPU times; always returns 0 ticks. */
static inline clock_t times(struct tms *buf) {
    if (buf) {
        buf->tms_utime  = 0;
        buf->tms_stime  = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return 0;
}

#endif /* _SYS_TIMES_H */
