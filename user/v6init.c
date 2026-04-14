// v6init: Legacy init without rc-script/runlevel support

#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"

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
}

int
main(void)
{
  int mpid, pid, wpid;

  dprintf(1, "init: starting up\n");

  dprintf(1, "init: attempting to open /dev/console\n");
  if(open("/dev/console", O_RDWR) < 0){
    dprintf(1, "init: /dev/console open failed, creating it\n");
    mknod("/dev/console", M_IFCHR, 1, 1);
    if(open("/dev/console", O_RDWR) < 0){
      dprintf(2, "init: cannot open /dev/console even after mknod\n");
    } else {
      dprintf(1, "init: /dev/console created and opened\n");
    }
  } else {
    dprintf(1, "init: /dev/console already exists\n");
  }

  dprintf(1, "init: creating /dev directory\n");
  mkdir("/dev");
  dprintf(1, "init: calling make_disk_nodes\n");
  make_disk_nodes();
  dprintf(1, "init: make_disk_nodes done\n");

  dprintf(1, "init: duping stdin to stdout and stderr\n");
  dup(0);  // stdout
  dup(0);  // stderr

  // Best-effort boot mounts from /etc/fstab.
  dprintf(1, "init: creating /proc directory\n");
  mkdir("/proc");
  dprintf(1, "init: creating /mnt directory\n");
  mkdir("/mnt");

  dprintf(1, "init: forking mount process\n");
  mpid = fork();
  if(mpid == 0){
    dprintf(1, "init: child executing mount\n");
    exec("/bin/mount", mount_argv);
    dprintf(1, "init: exec mount failed\n");
    exit(1);
  }
  if(mpid > 0){
    dprintf(1, "init: waiting for mount (pid %d)\n", mpid);
    int status;
    wait(&status);
    dprintf(1, "init: mount process exited\n");
  }

  dprintf(1, "init: entering main login loop\n");
  for(;;){
    dprintf(1, "init: starting login\n");
    pid = fork();
    if(pid < 0){
      dprintf(1, "init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      dprintf(1, "init: child executing login\n");
      exec("/bin/login", argv);
      dprintf(1, "init: exec login failed\n");
      exit(1);
    }
    int status;
    while((wpid=wait(&status)) >= 0 && wpid != pid){
      dprintf(1, "zombie! wpid=%d pid=%d\n", wpid, pid);
    }
  }
}
