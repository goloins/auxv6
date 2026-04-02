#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "sys/ioctl.h"

int
main(int argc, char **argv)
{
  int fd;
  int tty;

  fd = open("/dev/console", O_RDWR);
  if(fd < 0) {
    dprintf(2, "chvt: cannot open /dev/console\n");
    exit(1);
  }

  if(argc == 1) {
    tty = -1;
    if(ioctl(fd, TIOCGACTTTY, &tty) < 0) {
      dprintf(2, "chvt: failed to query active tty\n");
      close(fd);
      exit(1);
    }
    dprintf(1, "%d\n", tty);
    close(fd);
    exit(0);
  }

  tty = atoi(argv[1]);
  if(ioctl(fd, TIOCSACTTTY, tty) < 0) {
    dprintf(2, "chvt: failed to switch to tty %d\n", tty);
    close(fd);
    exit(1);
  }

  close(fd);
  exit(0);
  return 0;
}
