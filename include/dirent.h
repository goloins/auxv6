/*
 * <dirent.h> - POSIX directory entries
 *
 * Provides POSIX opendir/readdir/closedir API on top of the auxv6
 * getdents() syscall. The implementation lives in user/posix.c.
 *
 * The kernel's struct dirent is {ushort inum; char name[14]} (16 bytes each).
 * DIR.dd_buf holds KDIRENT_BUF=16 of those (256 bytes).
 */

#ifndef AUXV6_DIRENT_H
#define AUXV6_DIRENT_H

#include "sys/types.h"

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

struct dirent {
    ino_t d_ino;
    char  d_name[NAME_MAX + 1];
};

typedef struct {
    int           dd_fd;
    int           dd_loc;
    int           dd_size;
    char          dd_buf[256];
    struct dirent dd_ent;
} DIR;

DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
void           rewinddir(DIR *dirp);

#define dirent64   dirent
#define readdir64  readdir
#define opendir64  opendir
#define closedir64 closedir

#endif /* AUXV6_DIRENT_H */