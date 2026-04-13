/*
 * <time.h> - Time types and calendar conversion
 */

#ifndef _TIME_H
#define _TIME_H

#include "sys/types.h"
#include "sys/time.h"

typedef int clockid_t;

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

#define TIMER_ABSTIME   1

#define CLOCKS_PER_SEC 100

struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};

int         clock_gettime(clockid_t clock_id, struct timespec *tp);
int         clock_getres(clockid_t clock_id, struct timespec *res);
int         clock_settime(clockid_t clock_id, const struct timespec *tp);
int         clock_nanosleep(clockid_t clock_id, int flags,
                            const struct timespec *rqtp,
                            struct timespec *rmtp);
int         nanosleep(const struct timespec *rqtp, struct timespec *rmtp);
unsigned long long timespec_to_msec(const struct timespec *ts);
unsigned long long timespec_diff_msec(const struct timespec *start,
                                      const struct timespec *end);
time_t      time(time_t *tloc);
double      difftime(time_t time1, time_t time0);

struct tm  *gmtime(const time_t *timer);
struct tm  *gmtime_r(const time_t *timer, struct tm *result);
struct tm  *localtime(const time_t *timer);
struct tm  *localtime_r(const time_t *timer, struct tm *result);
time_t      mktime(struct tm *tm);

char       *asctime(const struct tm *tm);
char       *asctime_r(const struct tm *tm, char *buf);
char       *ctime(const time_t *timer);
char       *ctime_r(const time_t *timer, char *buf);

size_t      strftime(char *s, size_t max, const char *format,
                     const struct tm *tm);

#endif /* _TIME_H */