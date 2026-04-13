/*
 * dirent.c - POSIX directory iteration helpers split out of user/posix.c
 */

#include "types.h"
#include "fcntl.h"
#include "dirent.h"
#include "string.h"
#include "stdlib.h"
#include "auxv6/user.h"

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
  struct __auxv6_kdirent kbuf[AUXV6_KDIRENT_BATCH];
  int n;
  int i;

  if(dp == 0)
    return 0;

  for(;;){
    while(dp->dd_loc < dp->dd_size){
      struct __auxv6_kdirent *kd = dp->dd_buf + dp->dd_loc;

      dp->dd_loc++;
      if(kd->inum == 0)
        continue;
      dp->dd_ent.d_ino = (ino_t)kd->inum;
      memmove(dp->dd_ent.d_name, kd->name, sizeof(kd->name));
      dp->dd_ent.d_name[NAME_MAX] = '\0';
      return &dp->dd_ent;
    }

    n = getdents(dp->dd_fd, (struct dirent *)kbuf, AUXV6_KDIRENT_BATCH);
    if(n <= 0)
      return 0;

    for(i = 0; i < n; i++)
      dp->dd_buf[i] = kbuf[i];

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

int
alphasort(const struct dirent **a, const struct dirent **b)
{
  if(a == 0 || b == 0 || *a == 0 || *b == 0)
    return 0;
  return strcmp((*a)->d_name, (*b)->d_name);
}

static struct dirent*
dirent_dup(const struct dirent *de)
{
  struct dirent *copy;

  copy = (struct dirent*)malloc(sizeof(struct dirent));
  if(copy == 0)
    return 0;

  memmove(copy, de, sizeof(struct dirent));
  return copy;
}

int
scandir(const char *path, struct dirent ***namelist,
        int (*filter)(const struct dirent *),
        int (*compar)(const struct dirent **, const struct dirent **))
{
  DIR *dp;
  struct dirent *de;
  struct dirent **list;
  int cap;
  int n;
  int i;

  if(path == 0 || namelist == 0)
    return -1;

  dp = opendir(path);
  if(dp == 0)
    return -1;

  cap = 16;
  n = 0;
  list = (struct dirent**)malloc(sizeof(struct dirent*) * cap);
  if(list == 0) {
    closedir(dp);
    return -1;
  }

  while((de = readdir(dp)) != 0) {
    struct dirent *copy;

    if(filter && !filter(de))
      continue;

    copy = dirent_dup(de);
    if(copy == 0)
      goto fail;

    if(n >= cap) {
      struct dirent **newlist;
      int newcap;

      newcap = cap * 2;
      newlist = (struct dirent**)realloc(list, sizeof(struct dirent*) * newcap);
      if(newlist == 0) {
        free(copy);
        goto fail;
      }
      list = newlist;
      cap = newcap;
    }

    list[n++] = copy;
  }

  closedir(dp);

  if(compar) {
    qsort(list, n, sizeof(struct dirent*),
          (int (*)(const void*, const void*))compar);
  }

  *namelist = list;
  return n;

fail:
  closedir(dp);
  for(i = 0; i < n; i++)
    free(list[i]);
  free(list);
  return -1;
}