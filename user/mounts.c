#include "types.h"
#include "stat.h"
#include "auxv6/user.h"

int
main(int argc, char **argv)
{
  struct mountinfo entries[MOUNTINFO_MAX];
  int n;
  int i;

  if(argc != 1){
    printf(2, "usage: mounts\n");
    exit();
  }

  n = mountinfo(entries, MOUNTINFO_MAX);
  if(n < 0){
    printf(2, "mounts: mountinfo failed\n");
    exit();
  }

  printf(1, "dev flags fstype path\n");
  for(i = 0; i < n; i++)
    printf(1, "%d %d %s %s\n", entries[i].dev, entries[i].flags,
           entries[i].fstype, entries[i].path);

  exit();
}
