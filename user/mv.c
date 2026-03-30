#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 3){
    printf(2, "Usage: mv old new\n");
    exit();
  }

  if(rename(argv[1], argv[2]) < 0)
    printf(2, "mv: %s -> %s failed\n", argv[1], argv[2]);

  exit();
}
