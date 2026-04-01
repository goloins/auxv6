#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"
#include "../include/fcntl.h"
#include "../include/posix/sys/ioctl.h"

int
main(int argc, char **argv)
{
  int fd;
  int tty;

  fd = open("/dev/console", O_RDWR);
  if(fd < 0) {
    printf(2, "chvt: cannot open /dev/console\n");
    exit();
  }

  if(argc == 1) {
    tty = -1;
    if(ioctl(fd, TIOCGACTTTY, &tty) < 0) {
      printf(2, "chvt: failed to query active tty\n");
      close(fd);
      exit();
    }
    printf(1, "%d\n", tty);
    close(fd);
    exit();
  }

  tty = atoi(argv[1]);
  if(ioctl(fd, TIOCSACTTTY, tty) < 0) {
    printf(2, "chvt: failed to switch to tty %d\n", tty);
    close(fd);
    exit();
  }

  close(fd);
  exit();
  return 0;
}
