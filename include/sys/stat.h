/*
 * include/sys/stat.h — thin wrapper for auxv6's include/stat.h
 *
 * Placed here so that posix/sys/stat.h can reach the native struct stat
 * and M_IF* constants via #include_next <sys/stat.h>.
 */
#ifndef _SYS_STAT_NATIVE_H
#define _SYS_STAT_NATIVE_H

#include "../types.h"
#include "../stat.h"

#endif /* _SYS_STAT_NATIVE_H */
