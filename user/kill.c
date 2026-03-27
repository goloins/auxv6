#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"

int
main(int argc, char **argv)
{
  int sig;
  int i;
  int firstpid;

  if(argc < 2){
    printf(2, "usage: kill [-signo] pid...\n");
    exit();
  }

  sig = SIGTERM;
  firstpid = 1;
  if(argv[1][0] == '-') {
    sig = atoi(argv[1] + 1);
    firstpid = 2;
  }
  if(firstpid >= argc) {
    printf(2, "usage: kill [-signo] pid...\n");
    exit();
  }

  for(i = firstpid; i < argc; i++)
    sigsend(atoi(argv[i]), sig);
  exit();
}
