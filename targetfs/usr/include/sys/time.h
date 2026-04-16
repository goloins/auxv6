/*
 * <sys/time.h> - POSIX time structures
 */

#ifndef AUXV6_SYS_TIME_H
#define AUXV6_SYS_TIME_H

#include "sys/types.h"

#ifndef _SUSECONDS_T_DEFINED
#define _SUSECONDS_T_DEFINED
typedef long suseconds_t;
#endif

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

#ifndef USEC_PER_SEC
#define USEC_PER_SEC 1000000L
#endif

#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000L
#endif

#ifndef UTIME_NOW
#define UTIME_NOW  ((long)1073741823L)
#endif

#ifndef UTIME_OMIT
#define UTIME_OMIT ((long)1073741822L)
#endif

#ifndef timerisset
#define timerisset(tvp)      ((tvp)->tv_sec || (tvp)->tv_usec)
#endif

#ifndef timerclear
#define timerclear(tvp)      do { (tvp)->tv_sec = 0; (tvp)->tv_usec = 0; } while (0)
#endif

#ifndef timercmp
#define timercmp(a, b, CMP) \
    (((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_usec CMP (b)->tv_usec) : ((a)->tv_sec CMP (b)->tv_sec))
#endif

#ifndef timeradd
#define timeradd(a, b, res)                                                  \
    do {                                                                       \
        (res)->tv_sec = (a)->tv_sec + (b)->tv_sec;                              \
        (res)->tv_usec = (a)->tv_usec + (b)->tv_usec;                           \
        if ((res)->tv_usec >= USEC_PER_SEC) {                                   \
            (res)->tv_sec++;                                                       \
            (res)->tv_usec -= USEC_PER_SEC;                                       \
        }                                                                        \
    } while (0)
#endif

#ifndef timersub
#define timersub(a, b, res)                                                  \
    do {                                                                       \
        (res)->tv_sec = (a)->tv_sec - (b)->tv_sec;                              \
        (res)->tv_usec = (a)->tv_usec - (b)->tv_usec;                           \
        if ((res)->tv_usec < 0) {                                                \
            (res)->tv_sec--;                                                       \
            (res)->tv_usec += USEC_PER_SEC;                                       \
        }                                                                        \
    } while (0)
#endif

int gettimeofday(struct timeval *tv, struct timezone *tz);

#endif /* AUXV6_SYS_TIME_H */
