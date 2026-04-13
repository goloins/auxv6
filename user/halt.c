#include "types.h"
#include "auxv6/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 1){
    dprintf(2, "usage: halt\n");
    exit(1);
  }
  halt();
  dprintf(2, "halt: poweroff request returned\n");
  exit(1);
}