#include "auxv6/user.h"

int
main(int argc, char *argv[])
{
  (void)argv;
  if(argc > 1){
    dprintf(2, "usage: sync\n");
    return 1;
  }
  sync();
  return 0;
}
