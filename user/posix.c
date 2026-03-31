/*
 * posix.c - POSIX compatibility shims for auxv6 user space
 *
 * Implements:
 *   opendir / readdir / closedir / rewinddir
 *
 * The kernel's struct dirent uses {ushort inum; char name[DIRSIZ]} (DIRSIZ=14).
 * getdents() fills an array of those kernel dirents.
 * We translate each one into the POSIX struct dirent {ino_t d_ino; char d_name[]}.
 */

#include "../include/types.h"
#include "../include/fcntl.h"
#include "../include/stat.h"
#include "../include/user.h"
#include "../include/posix/dirent.h"
#include "../include/stdlib.h"
#include "../include/string.h"

/* Number of kernel dirents to fetch per getdents() call */
#define KDIRENT_BUF  16

/* Kernel-level dirent as seen by getdents(): matches fs.h */
struct kdirent {
  unsigned short inum;
  char name[14];   /* DIRSIZ = 14 */
};

DIR *
opendir(const char *path)
{
  int fd;
  DIR *dp;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return 0;

  dp = (DIR *)malloc(sizeof(DIR));
  if(dp == 0){
    close(fd);
    return 0;
  }
  dp->dd_fd    = fd;
  dp->dd_loc   = 0;
  dp->dd_size  = 0;
  return dp;
}

struct dirent *
readdir(DIR *dp)
{
  struct kdirent kbuf[KDIRENT_BUF];
  int n;
  int i;

  if(dp == 0)
    return 0;

  for(;;){
    /* Return next already-buffered entry */
    while(dp->dd_loc < dp->dd_size){
      struct kdirent *kd = (struct kdirent *)dp->dd_buf + dp->dd_loc;
      dp->dd_loc++;
      if(kd->inum == 0)
        continue;  /* deleted slot, skip */
      dp->dd_ent.d_ino = (ino_t)kd->inum;
      memmove(dp->dd_ent.d_name, kd->name, 14);
      dp->dd_ent.d_name[14] = '\0';
      return &dp->dd_ent;
    }

    /* Buffer exhausted — fetch more */
    n = getdents(dp->dd_fd, (struct dirent *)kbuf, KDIRENT_BUF);
    if(n <= 0)
      return 0;

    /* Copy raw kernel bytes into our buffer */
    for(i = 0; i < n; i++)
      *((struct kdirent *)dp->dd_buf + i) = kbuf[i];

    dp->dd_loc  = 0;
    dp->dd_size = n;
  }
}

int
closedir(DIR *dp)
{
  int fd;

  if(dp == 0)
    return -1;
  fd = dp->dd_fd;
  free(dp);
  return close(fd);
}

void
rewinddir(DIR *dp)
{
  if(dp == 0)
    return;
  /* Seek the underlying fd back to the beginning */
  lseek(dp->dd_fd, 0, 0 /* SEEK_SET */);
  dp->dd_loc  = 0;
  dp->dd_size = 0;
}
