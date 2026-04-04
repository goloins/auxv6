/*
 * <sys/types.h> - Data Types
 *
 * POSIX.1-2017 compatible type definitions
 */

#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include "stddef.h"
#include "stdint.h"

/* Process-related types */
typedef int             pid_t;      /* Process ID */
typedef int             uid_t;      /* User ID */
typedef int             gid_t;      /* Group ID */

/* File-related types */
typedef int             mode_t;     /* File mode (permissions) */
typedef unsigned int    ino_t;      /* Inode number */
typedef unsigned int    dev_t;      /* Device number */
typedef unsigned int    nlink_t;    /* Link count */
typedef int             off_t;      /* File offset (should be 64-bit for large files) */
typedef unsigned int    blksize_t;  /* Block size */
typedef unsigned int    blkcnt_t;   /* Block count */

/* BSD compatibility */
typedef unsigned int    u_int;
typedef unsigned short  u_short;
typedef unsigned char   u_char;
typedef unsigned long   u_long;

/* Time types */
typedef int             time_t;     /* Time in seconds since epoch */
typedef int             suseconds_t;/* Microseconds */
typedef unsigned int    useconds_t; /* Microseconds (unsigned) */
typedef int             clock_t;    /* Clock ticks */

/* IPC types */
typedef int             key_t;      /* IPC key */
typedef int             id_t;       /* Generic ID type */

/* Socket types */
typedef unsigned int    socklen_t;  /* Socket address length */
typedef int             sa_family_t;/* Address family */
#ifndef _NFDS_T
#define _NFDS_T
typedef unsigned int    nfds_t;     /* Number of fds in poll() */
#endif

/* Temporary pthread stand-ins pending real kernel/libc thread support. */
typedef unsigned int    pthread_t;
typedef unsigned int    pthread_attr_t;
typedef unsigned int    pthread_mutex_t;
typedef unsigned int    pthread_mutexattr_t;
typedef unsigned int    pthread_cond_t;
typedef unsigned int    pthread_condattr_t;
typedef unsigned int    pthread_key_t;
typedef unsigned int    pthread_once_t;
typedef unsigned int    pthread_rwlock_t;
typedef unsigned int    pthread_rwlockattr_t;

/* File descriptor set for select() */
#define FD_SETSIZE      64

typedef struct fd_set {
    unsigned long fds_bits[FD_SETSIZE / (8 * sizeof(unsigned long))];
} fd_set;

#define FD_ZERO(set)    memset((set), 0, sizeof(fd_set))
#define FD_SET(fd, set) ((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] |= \
                         (1UL << ((fd) % (8 * sizeof(unsigned long)))))
#define FD_CLR(fd, set) ((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] &= \
                         ~(1UL << ((fd) % (8 * sizeof(unsigned long)))))
#define FD_ISSET(fd, set) (((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] & \
                           (1UL << ((fd) % (8 * sizeof(unsigned long))))) != 0)

/* Major/minor device number macros */
#define major(dev)      ((unsigned int)(((dev) >> 8) & 0xFF))
#define minor(dev)      ((unsigned int)((dev) & 0xFF))
#define makedev(maj, min) ((dev_t)(((maj) << 8) | ((min) & 0xFF)))

#endif /* _SYS_TYPES_H */
