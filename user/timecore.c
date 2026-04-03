/*
 * timecore.c - wall-clock and broken-down time helpers
 */

#include "types.h"
#include "date.h"
#include "errno.h"
#include "string.h"
#include "sys/time.h"
#include "time.h"
#include "auxv6/user.h"

#define AUXV6_HZ 100
#define AUXV6_NSEC_PER_TICK 10000000L
#define AUXV6_NSEC_PER_SEC  1000000000L

static const char *const time_wday_short[] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char *const time_wday_long[] = {
  "Sunday", "Monday", "Tuesday", "Wednesday",
  "Thursday", "Friday", "Saturday"
};

static const char *const time_mon_short[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const char *const time_mon_long[] = {
  "January", "February", "March", "April", "May", "June",
  "July", "August", "September", "October", "November", "December"
};

static const int time_month_days[2][12] = {
  { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
  { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
};

static int
time_is_leap_year(int year)
{
  if((year % 4) != 0)
    return 0;
  if((year % 100) != 0)
    return 1;
  return (year % 400) == 0;
}

static int
time_days_in_year(int year)
{
  return time_is_leap_year(year) ? 366 : 365;
}

static int
time_days_in_month(int year, int mon)
{
  if(mon < 0 || mon >= 12)
    return 31;
  return time_month_days[time_is_leap_year(year)][mon];
}

static long long
time_days_before_year(int year)
{
  long long days;
  int y;

  days = 0;
  if(year >= 1970) {
    for(y = 1970; y < year; y++)
      days += time_days_in_year(y);
  } else {
    for(y = year; y < 1970; y++)
      days -= time_days_in_year(y);
  }

  return days;
}

static long long
time_days_before_month(int year, int mon)
{
  long long days;
  int i;

  days = 0;
  for(i = 0; i < mon; i++)
    days += time_days_in_month(year, i);
  return days;
}

static int
time_rtc_to_epoch(const struct rtcdate *r, time_t *out)
{
  long long days;
  long long seconds;

  if(r == 0 || out == 0) {
    errno = EINVAL;
    return -1;
  }
  if(r->month < 1 || r->month > 12 || r->day < 1 ||
     r->day > (uint)time_days_in_month((int)r->year, (int)r->month - 1) ||
     r->hour > 23 || r->minute > 59 || r->second > 60) {
    errno = EINVAL;
    return -1;
  }

  days = time_days_before_year((int)r->year);
  days += time_days_before_month((int)r->year, (int)r->month - 1);
  days += (long long)r->day - 1;

  seconds = days * 86400LL;
  seconds += (long long)r->hour * 3600LL;
  seconds += (long long)r->minute * 60LL;
  seconds += (long long)r->second;

  if(seconds < -2147483648LL || seconds > 2147483647LL) {
    errno = EOVERFLOW;
    return -1;
  }

  *out = (time_t)seconds;
  return 0;
}

static int
time_epoch_to_tm(time_t timer, struct tm *result)
{
  long long days;
  long long epoch_days;
  long long rem;
  int year;
  int yday;
  int mon;

  if(result == 0) {
    errno = EINVAL;
    return -1;
  }

  days = timer / 86400;
  rem = timer % 86400;
  if(rem < 0) {
    rem += 86400;
    days--;
  }
  epoch_days = days;

  year = 1970;
  if(days >= 0) {
    while(days >= time_days_in_year(year)) {
      days -= time_days_in_year(year);
      year++;
    }
  } else {
    do {
      year--;
      days += time_days_in_year(year);
    } while(days < 0);
  }

  yday = (int)days;
  mon = 0;
  while(mon < 11 && days >= time_days_in_month(year, mon)) {
    days -= time_days_in_month(year, mon);
    mon++;
  }

  result->tm_sec = (int)(rem % 60);
  rem /= 60;
  result->tm_min = (int)(rem % 60);
  rem /= 60;
  result->tm_hour = (int)rem;
  result->tm_mday = (int)days + 1;
  result->tm_mon = mon;
  result->tm_year = year - 1900;
  result->tm_wday = (int)((epoch_days + 4) % 7);
  if(result->tm_wday < 0)
    result->tm_wday += 7;
  result->tm_yday = yday;
  result->tm_isdst = 0;
  return 0;
}

static int
time_tm_to_epoch(const struct tm *tm, time_t *out)
{
  long long year;
  long long mon;
  long long days;
  long long seconds;

  if(tm == 0 || out == 0) {
    errno = EINVAL;
    return -1;
  }

  year = (long long)tm->tm_year + 1900LL;
  mon = (long long)tm->tm_mon;
  while(mon < 0) {
    mon += 12;
    year--;
  }
  while(mon >= 12) {
    mon -= 12;
    year++;
  }

  days = time_days_before_year((int)year);
  days += time_days_before_month((int)year, (int)mon);
  days += (long long)tm->tm_mday - 1;

  seconds = days * 86400LL;
  seconds += (long long)tm->tm_hour * 3600LL;
  seconds += (long long)tm->tm_min * 60LL;
  seconds += (long long)tm->tm_sec;

  if(seconds < -2147483648LL || seconds > 2147483647LL) {
    errno = EOVERFLOW;
    return -1;
  }

  *out = (time_t)seconds;
  return 0;
}

static int
time_timespec_valid(const struct timespec *ts)
{
  if(ts == 0)
    return 0;
  if(ts->tv_sec < 0)
    return 0;
  if(ts->tv_nsec < 0 || ts->tv_nsec >= AUXV6_NSEC_PER_SEC)
    return 0;
  return 1;
}

static void
time_timespec_normalize(struct timespec *ts)
{
  if(ts == 0)
    return;

  while(ts->tv_nsec >= AUXV6_NSEC_PER_SEC) {
    ts->tv_nsec -= AUXV6_NSEC_PER_SEC;
    ts->tv_sec++;
  }
  while(ts->tv_nsec < 0) {
    ts->tv_nsec += AUXV6_NSEC_PER_SEC;
    ts->tv_sec--;
  }
}

static void
time_timespec_add(struct timespec *dst, const struct timespec *a,
                  const struct timespec *b)
{
  dst->tv_sec = a->tv_sec + b->tv_sec;
  dst->tv_nsec = a->tv_nsec + b->tv_nsec;
  time_timespec_normalize(dst);
}

static void
time_timespec_sub(struct timespec *dst, const struct timespec *a,
                  const struct timespec *b)
{
  dst->tv_sec = a->tv_sec - b->tv_sec;
  dst->tv_nsec = a->tv_nsec - b->tv_nsec;
  time_timespec_normalize(dst);
}

static int
time_timespec_cmp(const struct timespec *a, const struct timespec *b)
{
  if(a->tv_sec < b->tv_sec)
    return -1;
  if(a->tv_sec > b->tv_sec)
    return 1;
  if(a->tv_nsec < b->tv_nsec)
    return -1;
  if(a->tv_nsec > b->tv_nsec)
    return 1;
  return 0;
}

static void
time_timespec_clear(struct timespec *ts)
{
  if(ts == 0)
    return;
  ts->tv_sec = 0;
  ts->tv_nsec = 0;
}

static void
time_timespec_to_timeval(const struct timespec *ts, struct timeval *tv)
{
  long usec;

  tv->tv_sec = ts->tv_sec;
  usec = (ts->tv_nsec + 999L) / 1000L;
  if(usec >= 1000000L) {
    tv->tv_sec++;
    usec -= 1000000L;
  }
  tv->tv_usec = (suseconds_t)usec;
}

static int
time_append_char(char *dst, size_t max, size_t *len, char ch)
{
  if(*len + 1 >= max)
    return 0;
  dst[*len] = ch;
  (*len)++;
  return 1;
}

static int
time_append_str(char *dst, size_t max, size_t *len, const char *src)
{
  while(*src != '\0') {
    if(!time_append_char(dst, max, len, *src))
      return 0;
    src++;
  }
  return 1;
}

static int
time_append_num(char *dst, size_t max, size_t *len,
                unsigned int value, int width, char pad)
{
  char tmp[16];
  int pos;

  pos = 0;
  do {
    tmp[pos++] = (char)('0' + (value % 10));
    value /= 10;
  } while(value != 0 && pos < (int)sizeof(tmp));

  while(pos < width && pos < (int)sizeof(tmp))
    tmp[pos++] = pad;

  while(pos > 0) {
    pos--;
    if(!time_append_char(dst, max, len, tmp[pos]))
      return 0;
  }
  return 1;
}

static int
time_append_format(char *dst, size_t max, size_t *len,
                   const char *format, const struct tm *tm)
{
  while(*format != '\0') {
    int year;
    int hour12;

    if(*format != '%') {
      if(!time_append_char(dst, max, len, *format++))
        return 0;
      continue;
    }

    format++;
    if(*format == '\0')
      return 0;

    switch(*format++) {
    case '%':
      if(!time_append_char(dst, max, len, '%'))
        return 0;
      break;
    case 'a':
      if(!time_append_str(dst, max, len, time_wday_short[tm->tm_wday]))
        return 0;
      break;
    case 'A':
      if(!time_append_str(dst, max, len, time_wday_long[tm->tm_wday]))
        return 0;
      break;
    case 'b':
      if(!time_append_str(dst, max, len, time_mon_short[tm->tm_mon]))
        return 0;
      break;
    case 'B':
      if(!time_append_str(dst, max, len, time_mon_long[tm->tm_mon]))
        return 0;
      break;
    case 'c':
      if(!time_append_format(dst, max, len, "%a %b %e %H:%M:%S %Y", tm))
        return 0;
      break;
    case 'C':
      year = tm->tm_year + 1900;
      if(year < 0)
        year = -year;
      if(!time_append_num(dst, max, len, (unsigned int)(year / 100), 2, '0'))
        return 0;
      break;
    case 'd':
      if(!time_append_num(dst, max, len, (unsigned int)tm->tm_mday, 2, '0'))
        return 0;
      break;
    case 'D':
      if(!time_append_format(dst, max, len, "%m/%d/%y", tm))
        return 0;
      break;
    case 'e':
      if(!time_append_num(dst, max, len, (unsigned int)tm->tm_mday, 2, ' '))
        return 0;
      break;
    case 'F':
      if(!time_append_format(dst, max, len, "%Y-%m-%d", tm))
        return 0;
      break;
    case 'H':
      if(!time_append_num(dst, max, len, (unsigned int)tm->tm_hour, 2, '0'))
        return 0;
      break;
    case 'I':
      hour12 = tm->tm_hour % 12;
      if(hour12 == 0)
        hour12 = 12;
      if(!time_append_num(dst, max, len, (unsigned int)hour12, 2, '0'))
        return 0;
      break;
    case 'j':
      if(!time_append_num(dst, max, len, (unsigned int)(tm->tm_yday + 1), 3, '0'))
        return 0;
      break;
    case 'm':
      if(!time_append_num(dst, max, len, (unsigned int)(tm->tm_mon + 1), 2, '0'))
        return 0;
      break;
    case 'M':
      if(!time_append_num(dst, max, len, (unsigned int)tm->tm_min, 2, '0'))
        return 0;
      break;
    case 'n':
      if(!time_append_char(dst, max, len, '\n'))
        return 0;
      break;
    case 'p':
      if(!time_append_str(dst, max, len, tm->tm_hour < 12 ? "AM" : "PM"))
        return 0;
      break;
    case 'R':
      if(!time_append_format(dst, max, len, "%H:%M", tm))
        return 0;
      break;
    case 'r':
      if(!time_append_format(dst, max, len, "%I:%M:%S %p", tm))
        return 0;
      break;
    case 'S':
      if(!time_append_num(dst, max, len, (unsigned int)tm->tm_sec, 2, '0'))
        return 0;
      break;
    case 'T':
      if(!time_append_format(dst, max, len, "%H:%M:%S", tm))
        return 0;
      break;
    case 't':
      if(!time_append_char(dst, max, len, '\t'))
        return 0;
      break;
    case 'u':
      if(!time_append_num(dst, max, len,
                          (unsigned int)(tm->tm_wday == 0 ? 7 : tm->tm_wday),
                          1, '0'))
        return 0;
      break;
    case 'w':
      if(!time_append_num(dst, max, len, (unsigned int)tm->tm_wday, 1, '0'))
        return 0;
      break;
    case 'x':
      if(!time_append_format(dst, max, len, "%m/%d/%y", tm))
        return 0;
      break;
    case 'X':
      if(!time_append_format(dst, max, len, "%H:%M:%S", tm))
        return 0;
      break;
    case 'y':
      year = tm->tm_year + 1900;
      if(year < 0)
        year = -year;
      if(!time_append_num(dst, max, len, (unsigned int)(year % 100), 2, '0'))
        return 0;
      break;
    case 'Y':
      year = tm->tm_year + 1900;
      if(year < 0) {
        if(!time_append_char(dst, max, len, '-'))
          return 0;
        year = -year;
      }
      if(!time_append_num(dst, max, len, (unsigned int)year, 4, '0'))
        return 0;
      break;
    case 'Z':
      if(!time_append_str(dst, max, len, "UTC"))
        return 0;
      break;
    default:
      if(!time_append_char(dst, max, len, '%') ||
         !time_append_char(dst, max, len, format[-1]))
        return 0;
      break;
    }
  }

  return 1;
}

int
gettimeofday(struct timeval *tv, struct timezone *tz)
{
  struct timespec ts;
  struct rtcdate rtc;
  time_t seconds;

  if(tz != 0) {
    tz->tz_minuteswest = 0;
    tz->tz_dsttime = 0;
  }
  if(tv == 0)
    return 0;

  if(__auxv6_sys_clock_gettime(CLOCK_REALTIME, &ts) == 0) {
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = (suseconds_t)(ts.tv_nsec / 1000L);
    return 0;
  }

  errno = 0;
  if(date(&rtc) < 0) {
    if(errno == 0)
      errno = EIO;
    return -1;
  }
  if(time_rtc_to_epoch(&rtc, &seconds) < 0)
    return -1;

  tv->tv_sec = seconds;
  tv->tv_usec = 0;
  return 0;
}

int
clock_gettime(clockid_t clock_id, struct timespec *tp)
{
  struct timeval tv;

  if(tp == 0) {
    errno = EINVAL;
    return -1;
  }

  switch(clock_id) {
  case CLOCK_REALTIME:
    if(__auxv6_sys_clock_gettime(clock_id, tp) == 0)
      return 0;
    if(gettimeofday(&tv, 0) < 0)
      return -1;
    tp->tv_sec = tv.tv_sec;
    tp->tv_nsec = (long)tv.tv_usec * 1000L;
    return 0;
  case CLOCK_MONOTONIC:
    if(__auxv6_sys_clock_gettime(clock_id, tp) < 0) {
      errno = EIO;
      return -1;
    }
    return 0;
  default:
    errno = EINVAL;
    return -1;
  }
}

int
clock_getres(clockid_t clock_id, struct timespec *res)
{
  if(res == 0)
    return 0;

  switch(clock_id) {
  case CLOCK_REALTIME:
    res->tv_sec = 1;
    res->tv_nsec = 0;
    return 0;
  case CLOCK_MONOTONIC:
    res->tv_sec = 0;
    res->tv_nsec = 1;
    return 0;
  default:
    errno = EINVAL;
    return -1;
  }
}

int
clock_settime(clockid_t clock_id, const struct timespec *tp)
{
  if(tp == 0 || !time_timespec_valid(tp)) {
    errno = EINVAL;
    return -1;
  }

  if(clock_id != CLOCK_REALTIME) {
    errno = EINVAL;
    return -1;
  }

  if(getuid() != 0) {
    errno = EPERM;
    return -1;
  }

  if(__auxv6_sys_clock_settime(clock_id, tp) < 0) {
    errno = EIO;
    return -1;
  }

  return 0;
}

int
clock_nanosleep(clockid_t clock_id, int flags,
                const struct timespec *rqtp, struct timespec *rmtp)
{
  struct timespec now;
  struct timespec target;
  struct timespec remain;
  struct timeval tv;

  if((flags & ~TIMER_ABSTIME) != 0)
    return EINVAL;
  if(!time_timespec_valid(rqtp))
    return EINVAL;
  if(clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC)
    return EOPNOTSUPP;

  if(flags & TIMER_ABSTIME) {
    target = *rqtp;
  } else {
    if(clock_gettime(clock_id, &now) < 0)
      return errno ? errno : EIO;
    time_timespec_add(&target, &now, rqtp);
  }

  for(;;) {
    if(clock_gettime(clock_id, &now) < 0)
      return errno ? errno : EIO;
    if(time_timespec_cmp(&now, &target) >= 0) {
      if((flags & TIMER_ABSTIME) == 0)
        time_timespec_clear(rmtp);
      return 0;
    }

    time_timespec_sub(&remain, &target, &now);
    time_timespec_to_timeval(&remain, &tv);
    if(select(0, 0, 0, 0, &tv) == 0) {
      if((flags & TIMER_ABSTIME) == 0)
        time_timespec_clear(rmtp);
      return 0;
    }

    if(errno != EINTR)
      return errno ? errno : EIO;

    if((flags & TIMER_ABSTIME) == 0 && rmtp != 0) {
      if(clock_gettime(clock_id, &now) < 0)
        time_timespec_clear(rmtp);
      else if(time_timespec_cmp(&now, &target) >= 0)
        time_timespec_clear(rmtp);
      else
        time_timespec_sub(rmtp, &target, &now);
    }
    return EINTR;
  }
}

int
nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
{
  int rc;

  rc = clock_nanosleep(CLOCK_MONOTONIC, 0, rqtp, rmtp);
  if(rc != 0) {
    errno = rc;
    return -1;
  }
  return 0;
}

unsigned long long
timespec_to_msec(const struct timespec *ts)
{
  unsigned long long sec_ms;
  unsigned long long nsec_ms;

  if(ts == 0)
    return 0;

  sec_ms = (unsigned long long)ts->tv_sec * 1000ULL;
  nsec_ms = (unsigned long long)ts->tv_nsec / 1000000ULL;
  return sec_ms + nsec_ms;
}

unsigned long long
timespec_diff_msec(const struct timespec *start, const struct timespec *end)
{
  struct timespec delta;

  if(start == 0 || end == 0)
    return 0;

  delta.tv_sec = end->tv_sec - start->tv_sec;
  delta.tv_nsec = end->tv_nsec - start->tv_nsec;
  if(delta.tv_nsec < 0) {
    delta.tv_sec--;
    delta.tv_nsec += 1000000000L;
  }
  if(delta.tv_sec < 0)
    return 0;

  return timespec_to_msec(&delta);
}

time_t
time(time_t *tloc)
{
  struct timeval tv;

  if(gettimeofday(&tv, 0) < 0)
    return (time_t)-1;
  if(tloc != 0)
    *tloc = tv.tv_sec;
  return tv.tv_sec;
}

double
difftime(time_t time1, time_t time0)
{
  return (double)time1 - (double)time0;
}

struct tm *
gmtime_r(const time_t *timer, struct tm *result)
{
  if(timer == 0 || result == 0) {
    errno = EINVAL;
    return 0;
  }
  if(time_epoch_to_tm(*timer, result) < 0)
    return 0;
  return result;
}

struct tm *
localtime_r(const time_t *timer, struct tm *result)
{
  return gmtime_r(timer, result);
}

struct tm *
gmtime(const time_t *timer)
{
  static struct tm tm_buf;

  return gmtime_r(timer, &tm_buf);
}

struct tm *
localtime(const time_t *timer)
{
  static struct tm tm_buf;

  return localtime_r(timer, &tm_buf);
}

time_t
mktime(struct tm *tm)
{
  struct tm normalized;
  time_t out;

  if(tm == 0) {
    errno = EINVAL;
    return (time_t)-1;
  }
  if(time_tm_to_epoch(tm, &out) < 0)
    return (time_t)-1;
  if(time_epoch_to_tm(out, &normalized) < 0)
    return (time_t)-1;
  *tm = normalized;
  return out;
}

char *
asctime_r(const struct tm *tm, char *buf)
{
  if(tm == 0 || buf == 0) {
    errno = EINVAL;
    return 0;
  }
  if(strftime(buf, 26, "%a %b %e %H:%M:%S %Y\n", tm) == 0) {
    errno = ERANGE;
    return 0;
  }
  return buf;
}

char *
ctime_r(const time_t *timer, char *buf)
{
  struct tm tm_buf;

  if(localtime_r(timer, &tm_buf) == 0)
    return 0;
  return asctime_r(&tm_buf, buf);
}

char *
asctime(const struct tm *tm)
{
  static char buf[26];

  return asctime_r(tm, buf);
}

char *
ctime(const time_t *timer)
{
  static char buf[26];

  return ctime_r(timer, buf);
}

size_t
strftime(char *s, size_t max, const char *format, const struct tm *tm)
{
  size_t len;

  if(s == 0 || format == 0 || tm == 0 || max == 0)
    return 0;
  if(tm->tm_wday < 0 || tm->tm_wday > 6 || tm->tm_mon < 0 || tm->tm_mon > 11) {
    s[0] = '\0';
    return 0;
  }

  len = 0;
  if(!time_append_format(s, max, &len, format, tm)) {
    s[0] = '\0';
    return 0;
  }

  s[len] = '\0';
  return len;
}