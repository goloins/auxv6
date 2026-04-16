/*
 * <ftw.h> - file tree walk APIs
 */

#ifndef _FTW_H
#define _FTW_H

#include "sys/stat.h"

struct FTW {
  int base;
  int level;
};

/* Type flags for callback */
#define FTW_F   0
#define FTW_D   1
#define FTW_DNR 2
#define FTW_DP  3
#define FTW_NS  4
#define FTW_SL  5
#define FTW_SLN 6

/* nftw flags */
#define FTW_PHYS  0x01
#define FTW_MOUNT 0x02
#define FTW_DEPTH 0x04
#define FTW_CHDIR 0x08

int nftw(const char *path,
         int (*fn)(const char *fpath, const struct stat *sb,
                   int typeflag, struct FTW *ftwbuf),
         int fd_limit,
         int flags);

#endif /* _FTW_H */