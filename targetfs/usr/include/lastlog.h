/*
 * <lastlog.h> - per-user last login record
 */

#ifndef _LASTLOG_H
#define _LASTLOG_H

#include "sys/types.h"

#ifndef LL_LINE_SIZE
#define LL_LINE_SIZE 32
#endif

#ifndef LL_HOST_SIZE
#define LL_HOST_SIZE 64
#endif

struct lastlog {
  time_t ll_time;
  char   ll_line[LL_LINE_SIZE];
  char   ll_host[LL_HOST_SIZE];
};

#endif /* _LASTLOG_H */
