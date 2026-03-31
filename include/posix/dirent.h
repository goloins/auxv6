/*
 * <dirent.h> - POSIX directory entries
 *
 * Provides POSIX opendir/readdir/closedir API on top of the auxv6
 * getdents() syscall.  The implementation lives in user/posix.c.
 *
 * The kernel's struct dirent is {ushort inum; char name[14]} (16 bytes each).
 * DIR.dd_buf holds KDIRENT_BUF=16 of those (256 bytes).
 */

#ifndef _DIRENT_H
#define _DIRENT_H

#include "sys/types.h"

#ifndef NAME_MAX
#define NAME_MAX    255
#endif

struct dirent {
    ino_t  d_ino;              /* inode number */
    char   d_name[NAME_MAX + 1]; /* null-terminated filename */
};

/*
 * DIR: internal state for directory streaming.
 * dd_buf stores raw kernel dirents (each 16 bytes); dd_ent is the
 * translated POSIX dirent returned to the caller.
 */
typedef struct {
    int           dd_fd;       /* open file descriptor */
    int           dd_loc;      /* next index to consume from dd_buf */
    int           dd_size;     /* valid entries in dd_buf */
    char          dd_buf[256]; /* raw kernel dirent buffer (16 × 16B) */
    struct dirent dd_ent;      /* staging area for translated entry */
} DIR;

DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
void           rewinddir(DIR *dirp);

/* 64-bit aliases — auxv6 has no large-file distinction */
#define readdir64   readdir
#define opendir64   opendir
#define closedir64  closedir

#endif /* _DIRENT_H */
