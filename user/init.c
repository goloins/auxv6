// init: The initial user-level program

#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"
#include "../include/fcntl.h"

char *argv[] = { "login", 0 };
char *mount_argv[] = { "mount", "/etc/fstab", 0 };

static void
ensure_node(const char *path, int mode, short major, short minor)
{
  int fd;
  struct stat st;

  fd = open(path, O_RDONLY);
  if(fd >= 0){
    close(fd);
    if(stat(path, &st) == 0 && st.type == T_DEV &&
       st.major == major && st.minor == minor &&
       (st.mode & M_IFMT) == (mode & M_IFMT))
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
}

int
main(void)
{
  int mpid, pid, wpid;

  if(open("/dev/console", O_RDWR) < 0){
    mknod("/dev/console", M_IFCHR, 1, 1);
    open("/dev/console", O_RDWR);
  }

  mkdir("/dev");
  make_disk_nodes();

  dup(0);  // stdout
  dup(0);  // stderr

  // Best-effort boot mounts from /etc/fstab.
  mkdir("/proc");
  mkdir("/mnt");
  mpid = fork();
  if(mpid == 0){
    exec("/bin/mount", mount_argv);
    printf(1, "init: exec mount failed\n");
    exit();
  }
  if(mpid > 0)
    wait();

  for(;;){
    printf(1, "init: starting login\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      exec("/bin/login", argv);
      printf(1, "init: exec login failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)
      printf(1, "zombie!\n");
  }
}
