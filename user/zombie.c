// Create a zombie process that
// must be reparented at exit.

#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"

int
main(void)
{
  int pid;

  pid = fork();
  if(pid < 0)
    exit(1);
  if(pid > 0)
    sleep(5);  // Let child exit before parent.
  exit(0);
}
