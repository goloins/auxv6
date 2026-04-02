/*
 * dirent.c - POSIX directory iteration helpers split out of user/posix.c
 */

#include "types.h"
#include "fcntl.h"
#include "dirent.h"
#include "auxv6/user.h"

#define KDIRENT_BUF  16

struct kdirent {
  unsigned short inum;
  char name[14];
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
  dp->dd_fd = fd;
  dp->dd_loc = 0;
  dp->dd_size = 0;
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
    while(dp->dd_loc < dp->dd_size){
      struct kdirent *kd = (struct kdirent *)dp->dd_buf + dp->dd_loc;

      dp->dd_loc++;
      if(kd->inum == 0)
        continue;
      dp->dd_ent.d_ino = (ino_t)kd->inum;
      memmove(dp->dd_ent.d_name, kd->name, 14);
      dp->dd_ent.d_name[14] = '\0';
      return &dp->dd_ent;
    }

    n = getdents(dp->dd_fd, (struct dirent *)kbuf, KDIRENT_BUF);
    if(n <= 0)
      return 0;

    for(i = 0; i < n; i++)
      *((struct kdirent *)dp->dd_buf + i) = kbuf[i];

    dp->dd_loc = 0;
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
  lseek(dp->dd_fd, 0, 0);
  dp->dd_loc = 0;
  dp->dd_size = 0;
}