// init: The initial user-level program

#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"
#include "../include/fcntl.h"

char *argv[] = { "login", 0 };
char *mount_argv[] = { "mount", "/etc/fstab", 0 };

int
main(void)
{
  int mpid, pid, wpid;

  if(open("/dev/console", O_RDWR) < 0){
    mknod("/dev/console", 1, 1);
    open("/dev/console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  // Best-effort boot mounts from /etc/fstab.
  mkdir("/proc");
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
