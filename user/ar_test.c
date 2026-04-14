#include "types.h"
#include "auxv6/user.h"
#include "stdlib.h"
#include "unistd.h"

int
main(int argc, char *argv[])
{
  if(argc < 2) {
    write(2, "need args\n", 10);
    exit(1);
  }
  
  write(1, "ok\n", 3);
  return 0;
}
