#include "types.h"
#include "user.h"
#include "stat.h"
#include "fcntl.h"

int
main(int argc, char *argv[])
{
  char *path;

  if(argc < 2) {
    printf(1, "Usage: umount <path>\n");
    exit();
  }

  path = argv[1];

  printf(1, "Unmounting %s...\n", path);
  int ret = umount(path);
  if(ret < 0) {
    printf(1, "umount failed\n");
  } else {
    printf(1, "umount succeeded\n");
  }

  exit();
}
