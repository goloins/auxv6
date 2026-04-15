/*
 * <sys/stat.h> - File status information structures and constants
 *
 * POSIX.1-2008 compatible stat structure with RFC 5280 time support (Tranche 1).
 * Canonical public interface for file metadata.
 *
 * Native implementation uses 64-bit time_t to support certificate
 * validity dates up to year 9999 per RFC 5280.
 */

#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include "sys/types.h"
#include "sys/time.h"

/* File type and mode bits */
#define S_IFMT      0170000  /* File type mask */
#define S_IFREG     0100000  /* Regular file */
#define S_IFDIR     0040000  /* Directory */
#define S_IFCHR     0020000  /* Character device */
#define S_IFBLK     0060000  /* Block device */
#define S_IFIFO     0010000  /* FIFO/named pipe */
#define S_IFLNK     0120000  /* Symbolic link */

/* File type test macros */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)

/* Permission mode bits */
#define S_ISVTX    01000  /* Sticky bit */
#define S_ISGID    02000  /* Set-group-ID on execution */
#define S_ISUID    04000  /* Set-user-ID on execution */

#define S_IRUSR    0400   /* User read permission */
#define S_IWUSR    0200   /* User write permission */
#define S_IXUSR    0100   /* User execute permission */
#define S_IRGRP    0040   /* Group read permission */
#define S_IWGRP    0020   /* Group write permission */
#define S_IXGRP    0010   /* Group execute permission */
#define S_IROTH    0004   /* Other read permission */
#define S_IWOTH    0002   /* Other write permission */
#define S_IXOTH    0001   /* Other execute permission */

/*
 * auxv6 compatibility aliases.
 * Keep legacy userspace code building while migrating call sites to S_* names.
 */
#define M_IFMT  S_IFMT
#define M_IFREG S_IFREG
#define M_IFDIR S_IFDIR
#define M_IFCHR S_IFCHR
#define M_IFBLK S_IFBLK
#define M_IFIFO S_IFIFO
#define M_IFLNK S_IFLNK

#define M_ISUID S_ISUID
#define M_ISGID S_ISGID
#define M_ISVTX S_ISVTX
#define M_IRUSR S_IRUSR
#define M_IWUSR S_IWUSR
#define M_IXUSR S_IXUSR
#define M_IRGRP S_IRGRP
#define M_IWGRP S_IWGRP
#define M_IXGRP S_IXGRP
#define M_IROTH S_IROTH
#define M_IWOTH S_IWOTH
#define M_IXOTH S_IXOTH

/* POSIX-compatible stat structure with 64-bit time_t (Tranche 1)
 *
 * Key improvements (Tranche 1):
 * - st_atime, st_mtime, st_ctime are time_t (64-bit long) for RFC 5280 compliance
 * - Supports certificate validity dates up to year 9999
 * - Padding and alignment follow POSIX conventions
 *
 * Note: Legacy 32-bit times removed. All timestamps >= 1970 and < year 9999.
 */
struct stat {
    dev_t       st_dev;       /* Device ID containing the file */
    ino_t       st_ino;       /* Inode number (unique within filesystem) */
    mode_t      st_mode;      /* File type and permission bits */
    nlink_t     st_nlink;     /* Number of hard links to the file */
    uid_t       st_uid;       /* User ID of file owner */
    gid_t       st_gid;       /* Group ID of file owner */
    dev_t       st_rdev;      /* Device ID (if special file) */
    off_t       st_size;      /* File size in bytes (64-bit) */
    time_t      st_atime;     /* Last access time (seconds since epoch, 64-bit) */
    time_t      st_mtime;     /* Last modification time (seconds since epoch, 64-bit) */
    time_t      st_ctime;     /* Last change time (seconds since epoch, 64-bit) */
    blksize_t   st_blksize;   /* Preferred I/O block size */
    blkcnt_t    st_blocks;    /* Number of 512-byte blocks allocated */
};

/* Function prototypes */
int stat(const char *restrict path, struct stat *restrict buf);
int fstat(int fd, struct stat *buf);
int lstat(const char *restrict path, struct stat *restrict buf);
int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags);
int futimens(int fd, const struct timespec times[2]);
mode_t umask(mode_t mask);
int mkdir(const char *path, mode_t mode);
int mkfifo(const char *path, mode_t mode);

#endif /* _SYS_STAT_H */
