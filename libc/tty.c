/*
 * tty.c - native auxv6 tty helpers split out of user/ulib.c
 */

#include "types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "errno.h"
#include "auxv6/user.h"
#include "sys/ioctl.h"

int grantpt(int fd);
int unlockpt(int fd);

char*
gets(char *buf, int max)
{
  int i;
  int cc;
  char c;

  for(i = 0; i + 1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

char*
readpass(char *buf, int max)
{
  struct termios oldt;
  struct termios newt;
  int i;
  int cc;
  char c;

  if(max <= 0)
    return buf;

  i = 0;
  if(tcgetattr(0, &oldt) < 0)
    return gets(buf, max);

  newt = oldt;
  newt.c_lflag &= ~(ECHO | ICANON);
  if(tcsetattr(0, TCSANOW, &newt) < 0)
    return gets(buf, max);

  while(i + 1 < max){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    if(c == '\r' || c == '\n')
      break;
    if(c == '\b' || c == '\x7f'){
      if(i > 0){
        i--;
        write(1, "\b \b", 3);
      }
      continue;
    }
    if(c == 4)
      break;
    buf[i++] = c;
    write(1, "*", 1);
  }
  buf[i] = 0;
  write(1, "\n", 1);
  tcsetattr(0, TCSANOW, &oldt);
  return buf;
}

int
isatty(int fd)
{
  struct termios t;

  if(fd < 0)
    return 0;
  return (tcgetattr(fd, &t) == 0) ? 1 : 0;
}

char*
ttyname(int fd)
{
  static char name[32];
  struct stat st;
  int ptn;
  int n;

  if(!isatty(fd))
    return 0;

  if(fstat(fd, &st) < 0 || !S_ISCHR(st.st_mode))
    return 0;

  if(major(st.st_rdev) == 1) {
    memmove(name, "/dev/console", 13);
    return name;
  }

  if(major(st.st_rdev) == 3 && minor(st.st_rdev) == 0) {
    memmove(name, "/dev/ptmx", 10);
    return name;
  }

  if(major(st.st_rdev) == 3 && minor(st.st_rdev) >= 1) {
    n = (int)minor(st.st_rdev) - 1;
    if(n < 10) {
      name[0] = '/'; name[1] = 'd'; name[2] = 'e'; name[3] = 'v';
      name[4] = '/'; name[5] = 'p'; name[6] = 't'; name[7] = 's';
      name[8] = '/'; name[9] = '0' + n; name[10] = 0;
      return name;
    }
    if(n < 100) {
      name[0] = '/'; name[1] = 'd'; name[2] = 'e'; name[3] = 'v';
      name[4] = '/'; name[5] = 'p'; name[6] = 't'; name[7] = 's';
      name[8] = '/'; name[9] = '0' + (n / 10); name[10] = '0' + (n % 10);
      name[11] = 0;
      return name;
    }
  }

  if(major(st.st_rdev) == 3 && ioctl(fd, TIOCGPTN, &ptn) == 0) {
    if(ptn < 0 || ptn >= 100)
      return 0;
    if(ptn < 10) {
      name[0] = '/'; name[1] = 'd'; name[2] = 'e'; name[3] = 'v';
      name[4] = '/'; name[5] = 'p'; name[6] = 't'; name[7] = 's';
      name[8] = '/'; name[9] = '0' + ptn; name[10] = 0;
    } else {
      name[0] = '/'; name[1] = 'd'; name[2] = 'e'; name[3] = 'v';
      name[4] = '/'; name[5] = 'p'; name[6] = 't'; name[7] = 's';
      name[8] = '/'; name[9] = '0' + (ptn / 10); name[10] = '0' + (ptn % 10);
      name[11] = 0;
    }
    return name;
  }

  return 0;
}

char*
ptsname(int fd)
{
  static char name[32];
  int ptn;

  if(ioctl(fd, TIOCGPTN, &ptn) < 0)
    return 0;
  if(ptn < 0 || ptn >= 100)
    return 0;

  if(ptn < 10) {
    name[0] = '/'; name[1] = 'd'; name[2] = 'e'; name[3] = 'v';
    name[4] = '/'; name[5] = 'p'; name[6] = 't'; name[7] = 's';
    name[8] = '/'; name[9] = '0' + ptn; name[10] = 0;
  } else {
    name[0] = '/'; name[1] = 'd'; name[2] = 'e'; name[3] = 'v';
    name[4] = '/'; name[5] = 'p'; name[6] = 't'; name[7] = 's';
    name[8] = '/'; name[9] = '0' + (ptn / 10); name[10] = '0' + (ptn % 10);
    name[11] = 0;
  }

  return name;
}

int
ptsname_r(int fd, char *buf, size_t buflen)
{
  char *name;
  uint need;

  if(buf == 0)
    return -1;

  name = ptsname(fd);
  if(name == 0)
    return -1;

  need = strlen(name) + 1;
  if(buflen < need)
    return -1;

  memmove(buf, name, need);
  return 0;
}

int
ttyname_r(int fd, char *buf, size_t buflen)
{
  char *name;
  uint need;

  if(buf == 0)
    return -1;

  name = ttyname(fd);
  if(name == 0)
    return -1;

  need = strlen(name) + 1;
  if(buflen < need)
    return -1;

  memmove(buf, name, need);
  return 0;
}

int
openpty(int *amaster, int *aslave, char *name,
        const struct termios *termp,
        const struct winsize *winp)
{
  int mfd;
  int sfd;
  char slave_name[32];

  if(amaster == 0 || aslave == 0) {
    errno = EINVAL;
    return -1;
  }

  mfd = open("/dev/ptmx", O_RDWR);
  if(mfd < 0) {
    errno = ENOENT;
    return -1;
  }

  if(grantpt(mfd) < 0 || unlockpt(mfd) < 0) {
    close(mfd);
    return -1;
  }

  if(ptsname_r(mfd, slave_name, sizeof(slave_name)) < 0) {
    close(mfd);
    errno = ENOENT;
    return -1;
  }

  sfd = open(slave_name, O_RDWR);
  if(sfd < 0) {
    close(mfd);
    errno = ENOENT;
    return -1;
  }

  if(termp) {
    if(ioctl(sfd, TCSETS, (void*)termp) < 0) {
      close(sfd);
      close(mfd);
      errno = EINVAL;
      return -1;
    }
  }

  if(winp) {
    if(ioctl(sfd, TIOCSWINSZ, (void*)winp) < 0) {
      close(sfd);
      close(mfd);
      errno = EINVAL;
      return -1;
    }
  }

  if(name)
    strcpy(name, slave_name);

  *amaster = mfd;
  *aslave = sfd;
  return 0;
}

int
posix_openpt(int flags)
{
  int oflags;

  if((flags & 0x3) != O_RDWR) {
    errno = EINVAL;
    return -1;
  }
  if((flags & ~(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC)) != 0) {
    errno = EINVAL;
    return -1;
  }

  oflags = O_RDWR | (flags & O_NONBLOCK);
  return open("/dev/ptmx", oflags);
}

int
grantpt(int fd)
{
  int ptn;

  if(ioctl(fd, TIOCGPTN, &ptn) < 0) {
    errno = EINVAL;
    return -1;
  }

  return 0;
}

int
unlockpt(int fd)
{
  int ptn;
  int lock;

  if(ioctl(fd, TIOCGPTN, &ptn) < 0) {
    errno = EINVAL;
    return -1;
  }

  lock = 0;
  if(ioctl(fd, TIOCSPTLCK, &lock) < 0) {
    errno = EINVAL;
    return -1;
  }

  return 0;
}

int
tcsendbreak(int fd, int duration)
{
  (void)fd;
  (void)duration;
  /* Break is not meaningful on a PTY or virtual console; nothing to do. */
  return 0;
}

speed_t
cfgetispeed(const struct termios *termios_p)
{
  if(termios_p == 0)
    return (speed_t)0;
  return (speed_t)(termios_p->c_cflag & 0010017U);
}

speed_t
cfgetospeed(const struct termios *termios_p)
{
  if(termios_p == 0)
    return (speed_t)0;
  return (speed_t)(termios_p->c_cflag & 0010017U);
}

int
cfsetispeed(struct termios *termios_p, speed_t speed)
{
  if(termios_p == 0) {
    errno = EINVAL;
    return -1;
  }
  termios_p->c_cflag &= ~0010017U;
  termios_p->c_cflag |= ((uint)speed & 0010017U);
  return 0;
}

int
cfsetospeed(struct termios *termios_p, speed_t speed)
{
  if(termios_p == 0) {
    errno = EINVAL;
    return -1;
  }
  termios_p->c_cflag &= ~0010017U;
  termios_p->c_cflag |= ((uint)speed & 0010017U);
  return 0;
}