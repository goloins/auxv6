#include "types.h"
#include "auxv6/user.h"

int
main(int argc, char **argv)
{
  int fd;

  (void)argc;
  (void)argv;

  fd = 0;
  if(!isatty(fd))
    fd = 1;
  if(!isatty(fd))
    fd = 2;

  write(fd, "\033[0m\033[2J\033[H", 12);
  exit(0);
  return 0;
}
