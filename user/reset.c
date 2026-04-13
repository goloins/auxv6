#include "types.h"
#include "auxv6/user.h"
#include "termios.h"
#include "sys/ioctl.h"

static void
set_sane_termios(int fd)
{
  struct termios t;

  if(tcgetattr(fd, &t) < 0)
    return;

  t.c_iflag = ICRNL;
  t.c_oflag = OPOST | ONLCR;
  t.c_cflag = CS8 | CREAD | CLOCAL;
  t.c_lflag = ECHO | ICANON | ISIG | ECHOE | IEXTEN;

  t.c_cc[VINTR] = 3;
  t.c_cc[VQUIT] = 28;
  t.c_cc[VERASE] = 127;
  t.c_cc[VKILL] = 21;
  t.c_cc[VEOF] = 4;
  t.c_cc[VTIME] = 0;
  t.c_cc[VMIN] = 1;
  t.c_cc[VSTART] = 17;
  t.c_cc[VSTOP] = 19;
  t.c_cc[VSUSP] = 26;
  t.c_cc[VEOL] = 0;

  tcsetattr(fd, TCSANOW, &t);
}

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

  set_sane_termios(fd);
  ioctl(fd, TCFLSH, TCIOFLUSH);

  write(fd, "\033c", 2);
  write(fd, "\033[0m\033[2J\033[H", 12);
  exit(0);
  return 0;
}
