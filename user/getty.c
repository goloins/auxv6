#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "sys/ioctl.h"

#define MAX_TTYS 8

static char *login_argv[] = { "login", 0 };

static int
spawn_login_on_tty(int tty)
{
  int pid;
  int pgrp;
  char tty_path[] = "/dev/tty0";

  pid = fork();
  if(pid != 0)
    return pid;

  close(0);
  close(1);
  close(2);
  if(tty < 0)
    tty = 0;
  if(tty > 9)
    tty = 9;
  tty_path[8] = '0' + tty;
  if(open(tty_path, O_RDWR) < 0)
    exit(1);
  dup(0);
  dup(0);

  setsid();
  setpgid(0, 0);
  ioctl(0, TIOCSCTTY, tty);

  pgrp = getpid();
  ioctl(0, TIOCSPGRP, &pgrp);

  exec("/bin/login", login_argv);
  dprintf(2, "getty: exec /bin/login failed on tty %d\n", tty);
  exit(1);
  return -1;
}

int
main(int argc, char **argv)
{
  int fd;
  int ntty;
  int active;
  int pids[MAX_TTYS];
  int i;
  int wpid;

  (void)argc;
  (void)argv;

  fd = open("/dev/console", O_RDWR);
  if(fd < 0) {
    dprintf(2, "getty: cannot open /dev/console\n");
    exit(1);
  }

  ntty = 4;
  if(ioctl(fd, TIOCGNTTY, &ntty) < 0)
    ntty = 4;
  if(ntty < 1)
    ntty = 1;
  if(ntty > MAX_TTYS)
    ntty = MAX_TTYS;

  active = 0;
  if(ioctl(fd, TIOCGACTTTY, &active) < 0)
    active = 0;
  if(active < 0 || active >= ntty)
    active = 0;
  ioctl(fd, TIOCSACTTTY, active);

  for(i = 0; i < ntty; i++)
    pids[i] = -1;

  for(i = 0; i < ntty; i++) {
    pids[i] = spawn_login_on_tty(i);
    if(pids[i] < 0)
      dprintf(2, "getty: fork failed for tty %d\n", i);
  }

  for(;;) {
    int status;
    wpid = wait(&status);
    if(wpid < 0) {
      sleep(20);
      for(i = 0; i < ntty; i++) {
        if(pids[i] < 0)
          pids[i] = spawn_login_on_tty(i);
      }
      continue;
    }

    for(i = 0; i < ntty; i++) {
      if(pids[i] != wpid)
        continue;
      pids[i] = spawn_login_on_tty(i);
      if(pids[i] < 0)
        dprintf(2, "getty: respawn failed for tty %d\n", i);
      break;
    }
  }

  return 0;
}
