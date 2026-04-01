// init: The initial user-level program

#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"
#include "../include/fcntl.h"
#include "../include/signal.h"

char *argv[] = { "login", 0 };
char *mount_argv[] = { "mount", "/etc/fstab", 0 };
char *rc_argv[] = { "dash", "/etc/rc.d/rc.S", 0 };

static volatile sig_atomic_t runlevel_update_pending = 0;

static void
on_sighup(int signo)
{
  if(signo == SIGHUP)
    runlevel_update_pending = 1;
}

static void
write_runlevel_state(char prev, char cur)
{
  int fd;
  char buf[4];

  fd = open("/etc/runlevel", O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0)
    return;
  buf[0] = prev;
  buf[1] = ' ';
  buf[2] = cur;
  buf[3] = '\n';
  write(fd, buf, sizeof(buf));
  close(fd);
}

static int
read_requested_runlevel(char *out)
{
  int fd, n, i;
  char buf[16];

  fd = open("/etc/.runlevel.req", O_RDONLY);
  if(fd < 0)
    return -1;
  n = read(fd, buf, sizeof(buf));
  close(fd);
  if(n <= 0)
    return -1;

  for(i = 0; i < n; i++){
    char c = buf[i];
    if(c == ' ' || c == '\t' || c == '\r' || c == '\n')
      continue;
    if(c == '0' || c == '1' || c == '2' || c == '3' ||
       c == '4' || c == '5' || c == '6' || c == 'S' || c == 's'){
      *out = (c == 's') ? 'S' : c;
      return 0;
    }
    return -1;
  }
  return -1;
}

static void
clear_requested_runlevel(void)
{
  unlink("/etc/.runlevel.req");
}

static void
run_runlevel_script(char target)
{
  int pid;
  char script_path[] = "/etc/rc.d/rc.X";
  char *script_argv[] = { "dash", script_path, 0 };

  script_path[13] = target;
  pid = fork();
  if(pid == 0){
    printf(1, "init: running runlevel script %s\n", script_path);
    exec("/bin/dash", script_argv);
    printf(1, "init: runlevel script %s missing or failed\n", script_path);
    exit();
  }
  if(pid > 0)
    wait();
}

static void
maybe_process_runlevel_change(int *login_pid, char *cur_runlevel)
{
  char target;
  char prev;

  if(!runlevel_update_pending)
    return;

  runlevel_update_pending = 0;
  if(read_requested_runlevel(&target) < 0)
    return;

  clear_requested_runlevel();
  if(target == *cur_runlevel)
    return;

  prev = *cur_runlevel;
  printf(1, "init: runlevel transition %c -> %c\n", prev, target);
  if(*login_pid > 0)
    sigsend(*login_pid, SIGTERM);
  run_runlevel_script(target);
  *cur_runlevel = target;
  write_runlevel_state(prev, *cur_runlevel);
}

static void
ensure_node(const char *path, int mode, short major, short minor)
{
  int fd;
  struct stat st;

  fd = open(path, O_RDONLY);
  if(fd >= 0){
    close(fd);
    if(stat(path, &st) == 0 && st.st_type == T_DEV &&
       st.st_major == major && st.st_minor == minor &&
       (st.st_mode & M_IFMT) == (mode & M_IFMT))
      return;
    unlink(path);
  }
  mknod((char*)path, mode, major, minor);
}

static void
make_disk_nodes(void)
{
  int unit;
  int part;
  int dev;
  char path[16];

  for(unit = 0; unit < HD_DISK_UNITS; unit++){
    dev = HD_DISK_DEV(unit);
    if(devblocks(dev) <= 0)
      continue;

    path[0] = '/'; path[1] = 'd'; path[2] = 'e'; path[3] = 'v'; path[4] = '/';
    path[5] = 'h'; path[6] = 'd'; path[7] = 'a' + unit; path[8] = 0;
    ensure_node(path, M_IFBLK, 2, dev);

    for(part = 1; part <= HD_PARTS_PER_DISK; part++){
      int pdev = HD_PART_DEV(unit, part);
      if(devblocks(pdev) <= 0)
        continue;
      path[0] = '/'; path[1] = 'd'; path[2] = 'e'; path[3] = 'v'; path[4] = '/';
      path[5] = 'h'; path[6] = 'd'; path[7] = 'a' + unit;
      path[8] = '0' + part;
      path[9] = 0;
      ensure_node(path, M_IFBLK, 2, pdev);
    }
  }

  for(unit = 0; unit < VD_DISK_UNITS; unit++){
    dev = VD_DISK_DEV(unit);
    if(devblocks(dev) <= 0)
      continue;

    path[0] = '/'; path[1] = 'd'; path[2] = 'e'; path[3] = 'v'; path[4] = '/';
    path[5] = 'v'; path[6] = 'd'; path[7] = 'a' + unit; path[8] = 0;
    ensure_node(path, M_IFBLK, 2, dev);

    for(part = 1; part <= VD_PARTS_PER_DISK; part++){
      int pdev = VD_PART_DEV(unit, part);
      if(devblocks(pdev) <= 0)
        continue;
      path[0] = '/'; path[1] = 'd'; path[2] = 'e'; path[3] = 'v'; path[4] = '/';
      path[5] = 'v'; path[6] = 'd'; path[7] = 'a' + unit;
      path[8] = '0' + part;
      path[9] = 0;
      ensure_node(path, M_IFBLK, 2, pdev);
    }
  }

  for(unit = 0; unit < ND_DISK_UNITS; unit++){
    dev = ND_DISK_DEV(unit);
    if(devblocks(dev) <= 0)
      continue;

    path[0] = '/'; path[1] = 'd'; path[2] = 'e'; path[3] = 'v'; path[4] = '/';
    path[5] = 'n'; path[6] = 'd'; path[7] = 'a' + unit; path[8] = 0;
    ensure_node(path, M_IFBLK, 2, dev);
  }
}

static void
make_tty_nodes(void)
{
  int i;
  char path[] = "/dev/tty0";
  char pty_path[] = "/dev/pts/00";

  for(i = 0; i < 4; i++) {
    path[8] = '0' + i;
    ensure_node(path, M_IFCHR, 1, i + 1);
  }

  mkdir("/dev/pts");
  ensure_node("/dev/ptmx", M_IFCHR, 3, 0);
  for(i = 0; i < 16; i++) {
    if(i < 10) {
      pty_path[9] = '0' + i;
      pty_path[10] = 0;
    } else {
      pty_path[9] = '0' + (i / 10);
      pty_path[10] = '0' + (i % 10);
      pty_path[11] = 0;
    }
    ensure_node(pty_path, M_IFCHR, 3, i + 1);
  }
}

int
main(void)
{
  int cpid, pid, wpid;
  char cur_runlevel;
  struct sigaction sa;

  printf(1, "init: starting up\n");

  printf(1, "init: attempting to open /dev/console\n");
  if(open("/dev/console", O_RDWR) < 0){
    printf(1, "init: /dev/console open failed, creating it\n");
    mknod("/dev/console", M_IFCHR, 1, 1);
    if(open("/dev/console", O_RDWR) < 0){
      printf(2, "init: cannot open /dev/console even after mknod\n");
    } else {
      printf(1, "init: /dev/console created and opened\n");
    }
  } else {
    printf(1, "init: /dev/console already exists\n");
  }

  printf(1, "init: creating /dev directory\n");
  mkdir("/dev");
  make_tty_nodes();
  printf(1, "init: calling make_disk_nodes\n");
  make_disk_nodes();
  printf(1, "init: make_disk_nodes done\n");

  printf(1, "init: duping stdin to stdout and stderr\n");
  dup(0);  // stdout
  dup(0);  // stderr

  // Best-effort boot mounts from /etc/fstab.
  printf(1, "init: creating /proc directory\n");
  mkdir("/proc");
  printf(1, "init: creating /mnt directory\n");
  mkdir("/mnt");
  
  printf(1, "init: forking mount process\n");
  cpid = fork();
  if(cpid == 0){
    printf(1, "init: child executing mount\n");
    exec("/bin/mount", mount_argv);
    printf(1, "init: exec mount failed\n");
    exit();
  }
  if(cpid > 0){
    printf(1, "init: waiting for mount (pid %d)\n", cpid);
    wait();
    printf(1, "init: mount process exited\n");
  }

  printf(1, "init: forking rc script process\n");
  cpid = fork();
  if(cpid == 0){
    printf(1, "init: child executing /etc/rc.d/rc.S with /bin/dash\n");
    exec("/bin/dash", rc_argv);
    printf(1, "init: rc script missing or failed; continuing\n");
    exit();
  }
  if(cpid > 0){
    printf(1, "init: waiting for rc script (pid %d)\n", cpid);
    wait();
    printf(1, "init: rc script process exited\n");
  }

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_sighup;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGHUP, &sa, 0);

  cur_runlevel = '3';
  write_runlevel_state('N', cur_runlevel);
  run_runlevel_script(cur_runlevel);

  pid = -1;
  printf(1, "init: entering main login loop\n");
  for(;;){
    maybe_process_runlevel_change(&pid, &cur_runlevel);
    printf(1, "init: starting login\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      printf(1, "init: child executing login\n");
      exec("/bin/login", argv);
      printf(1, "init: exec login failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid){
      maybe_process_runlevel_change(&pid, &cur_runlevel);
      printf(1, "zombie! wpid=%d pid=%d\n", wpid, pid);
    }
    pid = -1;
    maybe_process_runlevel_change(&pid, &cur_runlevel);
  }
}
