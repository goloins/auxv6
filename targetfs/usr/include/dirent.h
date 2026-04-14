/*
 * <dirent.h> - POSIX directory entries
 *
 * Provides POSIX opendir/readdir/closedir API on top of the auxv6
 * getdents() syscall. The implementation lives in user/posix.c.
 *
 * The kernel's getdents() ABI uses fixed-size dirent records with NAME_MAX
 * bytes of payload plus a trailing NUL.
 */

#ifndef AUXV6_DIRENT_H
#define AUXV6_DIRENT_H

#include "sys/types.h"

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#define AUXV6_KDIRENT_BATCH 8

struct __auxv6_kdirent {
    unsigned short inum;
    char           name[NAME_MAX + 1];
};

struct dirent {
    ino_t d_ino;
    char  d_name[NAME_MAX + 1];
};

typedef struct {
    int           dd_fd;
    int           dd_loc;
    int           dd_size;
    struct __auxv6_kdirent dd_buf[AUXV6_KDIRENT_BATCH];
    struct dirent dd_ent;
} DIR;

DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
void           rewinddir(DIR *dirp);
int            scandir(const char *path, struct dirent ***namelist,
                       int (*filter)(const struct dirent *),
                       int (*compar)(const struct dirent **,
                                     const struct dirent **));
int            alphasort(const struct dirent **a, const struct dirent **b);

#define dirent64   dirent
#define readdir64  readdir
#define opendir64  opendir
#define closedir64 closedir

#endif /* AUXV6_DIRENT_H */