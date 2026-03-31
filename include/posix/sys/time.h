/*
 * <sys/time.h> - POSIX time structures
 */

#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include "sys/types.h"

struct timeval {
    time_t      tv_sec;     /* seconds */
    suseconds_t tv_usec;    /* microseconds */
};

struct timezone {
    int tz_minuteswest; /* minutes west of Greenwich */
    int tz_dsttime;     /* type of DST correction */
};

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

/* gettimeofday: not yet implemented; stub */
static inline int gettimeofday(struct timeval *tv, struct timezone *tz) {
    (void)tz;
    if (tv) { tv->tv_sec = 0; tv->tv_usec = 0; }
    return 0;
}

#endif /* _SYS_TIME_H */
