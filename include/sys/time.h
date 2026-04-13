/*
 * <sys/time.h> - POSIX time structures
 */

#ifndef AUXV6_SYS_TIME_H
#define AUXV6_SYS_TIME_H

#include "sys/types.h"

struct timeval {
    time_t      tv_sec;
    suseconds_t tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

int gettimeofday(struct timeval *tv, struct timezone *tz);

#endif /* AUXV6_SYS_TIME_H */
