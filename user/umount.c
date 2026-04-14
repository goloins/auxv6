#include "types.h"
#include "auxv6/user.h"
#include "sys/stat.h"
#include "fcntl.h"

int
main(int argc, char *argv[])
{
  char *path;
  int ret;

  if(argc < 2) {
    dprintf(1, "Usage: umount <path>\n");
    exit(1);
  }

  path = argv[1];

  dprintf(1, "Unmounting %s...\n", path);
  ret = umount(path);
  if(ret < 0) {
    dprintf(1, "umount failed\n");
    exit(1);
  } else {
    dprintf(1, "umount succeeded\n");
  }

  exit(0);
}
