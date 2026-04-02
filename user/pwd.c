#include "types.h"
#include "stat.h"
#include "auxv6/user.h"

int
main(void)
{
  char buf[128];

  if(getcwd(buf, sizeof(buf)) < 0){
    printf(2, "pwd: getcwd failed\n");
    exit();
  }

  printf(1, "%s\n", buf);
  exit();
}