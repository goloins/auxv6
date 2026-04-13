#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"

int
main(void)
{
  int fd;
  int n;
  char buf[8];
  char prev = 'N';
  char cur = 'N';

  fd = open("/etc/runlevel", O_RDONLY);
  if(fd >= 0){
    n = read(fd, buf, sizeof(buf));
    close(fd);
    if(n >= 3){
      prev = buf[0];
      cur = buf[2];
    }
  }

  dprintf(1, "%c %c\n", prev, cur);
  exit(0);
}
