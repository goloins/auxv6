#include "types.h"
#include "user.h"
#include "fcntl.h"

int
main(int argc, char *argv[])
{
  int fd;
  int n;
  char buf[256];

  if(argc != 1){
    printf(2, "usage: ps\n");
    exit();
  }

  fd = open("/proc/ps", O_RDONLY);
  if(fd < 0){
    printf(2, "ps: cannot open /proc/ps\n");
    exit();
  }

  while((n = read(fd, buf, sizeof(buf))) > 0)
    write(1, buf, n);

  close(fd);
  exit();
}
