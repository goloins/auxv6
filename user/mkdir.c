#include "types.h"
#include "stat.h"
#include "auxv6/user.h"

int
main(int argc, char *argv[])
{
  int i;
  int status;

  status = 0;

  if(argc < 2){
    dprintf(2, "Usage: mkdir files...\n");
    exit(1);
  }

  for(i = 1; i < argc; i++){
    if(mkdir(argv[i]) < 0){
      dprintf(2, "mkdir: %s failed to create\n", argv[i]);
      status = 1;
      break;
    }
  }

  exit(status);
}
