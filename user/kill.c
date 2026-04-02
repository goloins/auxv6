#include "types.h"
#include "stat.h"
#include "auxv6/user.h"

int
main(int argc, char **argv)
{
  int sig;
  int i;
  int firstpid;

  if(argc < 2){
    dprintf(2, "usage: kill [-signo] pid...\n");
    exit(1);
  }

  sig = SIGTERM;
  firstpid = 1;
  if(argv[1][0] == '-') {
    sig = atoi(argv[1] + 1);
    firstpid = 2;
  }
  if(firstpid >= argc) {
    dprintf(2, "usage: kill [-signo] pid...\n");
    exit(1);
  }

  for(i = firstpid; i < argc; i++)
    sigsend(atoi(argv[i]), sig);
  exit(0);
}
