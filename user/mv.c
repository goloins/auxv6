#include "types.h"
#include "stat.h"
#include "auxv6/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 3){
    dprintf(2, "Usage: mv old new\n");
    exit(0);
  }

  if(rename(argv[1], argv[2]) < 0)
    dprintf(2, "mv: %s -> %s failed\n", argv[1], argv[2]);

  exit(0);
}
