/*
 * <sys/resource.h> - POSIX resource limits
 */

#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#include "types.h"
#include "sys/time.h"

typedef unsigned long rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
};

#define RLIM_INFINITY   ((rlim_t)~0UL)

#define RLIMIT_CPU      0
#define RLIMIT_FSIZE    1
#define RLIMIT_DATA     2
#define RLIMIT_STACK    3
#define RLIMIT_CORE     4
#define RLIMIT_RSS      5
#define RLIMIT_NOFILE   7
#define RLIMIT_AS       9

#define RLIMIT_NLIMITS  10

#define RUSAGE_SELF      0
#define RUSAGE_CHILDREN -1

int getrlimit(int resource, struct rlimit *rlp);
int setrlimit(int resource, const struct rlimit *rlp);
int getrusage(int who, struct rusage *usage);

#endif /* _SYS_RESOURCE_H */