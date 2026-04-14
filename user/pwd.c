#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"

int
main(void)
{
  char buf[128];

  if(getcwd(buf, sizeof(buf)) == 0){
    dprintf(2, "pwd: getcwd failed\n");
    exit(0);
  }

  dprintf(1, "%s\n", buf);
  exit(0);
}