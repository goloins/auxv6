#include "types.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 1){
    printf(2, "usage: halt\n");
    exit();
  }
  halt();
  printf(2, "halt: poweroff request returned\n");
  exit();
}