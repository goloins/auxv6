/*
 * <sys/resource.h> - POSIX resource limits
 */

#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#include "types.h"

typedef unsigned long rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
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

static inline int getrlimit(int resource, struct rlimit *rlp) {
    (void)resource;
    (void)rlp;
    return -1;
}

static inline int setrlimit(int resource, const struct rlimit *rlp) {
    (void)resource;
    (void)rlp;
    return -1;
}

#endif /* _SYS_RESOURCE_H */