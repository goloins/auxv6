#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include "types.h"

struct timeval {
  int tv_sec;
  int tv_usec;
};

struct timezone {
  int tz_minuteswest;
  int tz_dsttime;
};

struct timespec {
  int tv_sec;
  long tv_nsec;
};

#endif
