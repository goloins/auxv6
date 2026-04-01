#include "../include/types.h"
#include "../include/user.h"
#include "../include/termios.h"
#include "../include/posix/sys/ioctl.h"

static int
check_termios_roundtrip(int fd)
{
  struct termios oldt;
  struct termios t;
  struct termios got;

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag &= ~(ECHO | ICANON);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;

  if(tcsetattr(fd, TCSANOW, &t) < 0)
    return -1;
  if(tcgetattr(fd, &got) < 0)
    return -1;

  if((got.c_lflag & (ECHO | ICANON)) != 0 ||
     got.c_cc[VMIN] != 0 ||
     got.c_cc[VTIME] != 1) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  tcsetattr(fd, TCSANOW, &oldt);
  return 0;
}

static int
check_noncanon_vmin0_vtime0(int fd)
{
  struct termios oldt;
  struct termios t;
  char ch;
  int n;

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;

  if(tcsetattr(fd, TCSANOW, &t) < 0)
    return -1;

  n = read(fd, &ch, 1);
  tcsetattr(fd, TCSANOW, &oldt);

  if(n != 0)
    return -1;
  return 0;
}

static int
check_winsize_ioctl(int fd)
{
  struct winsize oldw;
  struct winsize testw;
  struct winsize gotw;

  if(ioctl(fd, TIOCGWINSZ, &oldw) < 0)
    return -1;

  testw = oldw;
  testw.ws_row = 24;
  testw.ws_col = 80;

  if(ioctl(fd, TIOCSWINSZ, &testw) < 0)
    return -1;
  if(ioctl(fd, TIOCGWINSZ, &gotw) < 0)
    return -1;

  (void)ioctl(fd, TIOCSWINSZ, &oldw);

  if(gotw.ws_row != testw.ws_row || gotw.ws_col != testw.ws_col)
    return -1;
  return 0;
}

static int
check_tcsetattr_optional_actions(int fd)
{
  struct termios oldt;
  struct termios t;
  struct termios got;

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_lflag ^= ECHO;
  if(tcsetattr(fd, TCSADRAIN, &t) < 0)
    return -1;
  if(tcgetattr(fd, &got) < 0)
    return -1;
  if((got.c_lflag & ECHO) != (t.c_lflag & ECHO)) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  t = oldt;
  t.c_lflag ^= ECHO;
  if(tcsetattr(fd, TCSAFLUSH, &t) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }
  if(tcgetattr(fd, &got) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }
  if((got.c_lflag & ECHO) != (t.c_lflag & ECHO)) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  tcsetattr(fd, TCSANOW, &oldt);
  return 0;
}

static int
check_flag_roundtrip(int fd)
{
  struct termios oldt;
  struct termios t;
  struct termios got;

  if(tcgetattr(fd, &oldt) < 0)
    return -1;

  t = oldt;
  t.c_iflag ^= ISTRIP;
  t.c_lflag ^= ECHOCTL;

  if(tcsetattr(fd, TCSANOW, &t) < 0)
    return -1;
  if(tcgetattr(fd, &got) < 0) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  if((got.c_iflag & ISTRIP) != (t.c_iflag & ISTRIP) ||
     (got.c_lflag & ECHOCTL) != (t.c_lflag & ECHOCTL)) {
    tcsetattr(fd, TCSANOW, &oldt);
    return -1;
  }

  tcsetattr(fd, TCSANOW, &oldt);
  return 0;
}

int
main(int argc, char **argv)
{
  int fd;
  int fails;

  (void)argc;
  (void)argv;

  fd = 0;
  if(!isatty(fd))
    fd = 1;
  if(!isatty(fd))
    fd = 2;
  if(!isatty(fd)) {
    printf(2, "termcheck: no tty fd available\n");
    exit();
  }

  fails = 0;

  if(check_termios_roundtrip(fd) < 0) {
    printf(2, "FAIL: termios roundtrip\n");
    fails++;
  } else {
    printf(1, "PASS: termios roundtrip\n");
  }

  if(check_noncanon_vmin0_vtime0(fd) < 0) {
    printf(2, "FAIL: noncanon VMIN=0 VTIME=0 immediate read\n");
    fails++;
  } else {
    printf(1, "PASS: noncanon VMIN=0 VTIME=0 immediate read\n");
  }

  if(check_winsize_ioctl(fd) < 0) {
    printf(2, "FAIL: winsize ioctl\n");
    fails++;
  } else {
    printf(1, "PASS: winsize ioctl\n");
  }

  if(check_tcsetattr_optional_actions(fd) < 0) {
    printf(2, "FAIL: tcsetattr optional actions\n");
    fails++;
  } else {
    printf(1, "PASS: tcsetattr optional actions\n");
  }

  if(check_flag_roundtrip(fd) < 0) {
    printf(2, "FAIL: ISTRIP/ECHOCTL flag roundtrip\n");
    fails++;
  } else {
    printf(1, "PASS: ISTRIP/ECHOCTL flag roundtrip\n");
  }

  if(fails == 0)
    printf(1, "termcheck: all checks passed\n");
  else
    printf(2, "termcheck: %d checks failed\n", fails);

  exit();
  return 0;
}
