#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"

static int
valid_runlevel(char c)
{
  return c == '0' || c == '1' || c == '2' || c == '3' ||
         c == '4' || c == '5' || c == '6' || c == 'S' || c == 's';
}

int
main(int argc, char **argv)
{
  int fd;
  char rl;

  if(argc != 2 || argv[1][0] == 0 || argv[1][1] != 0){
    dprintf(2, "usage: telinit <0|1|2|3|4|5|6|S>\n");
    exit(1);
  }

  rl = argv[1][0];
  if(!valid_runlevel(rl)){
    dprintf(2, "telinit: invalid runlevel %c\n", rl);
    exit(1);
  }
  if(rl == 's')
    rl = 'S';

  fd = open("/etc/.runlevel.req", O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0){
    dprintf(2, "telinit: cannot write /etc/.runlevel.req\n");
    exit(1);
  }
  write(fd, &rl, 1);
  write(fd, "\n", 1);
  close(fd);

  if(sigsend(1, SIGHUP) < 0){
    dprintf(2, "telinit: cannot signal init\n");
    exit(1);
  }

  exit(0);
}
