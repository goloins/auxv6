#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"

int
main(int argc, char *argv[])
{
  int fd;
  int n;
  char buf[256];

  if(argc != 1){
    dprintf(2, "usage: ps\n");
    exit(1);
  }

  fd = open("/proc/ps", O_RDONLY);
  if(fd < 0){
    dprintf(2, "ps: cannot open /proc/ps\n");
    exit(1);
  }

  while((n = read(fd, buf, sizeof(buf))) > 0)
    write(1, buf, n);

  if(n < 0){
    close(fd);
    dprintf(2, "ps: read error\n");
    exit(1);
  }

  close(fd);
  exit(0);
}
