#include "types.h"
#include "auxv6/user.h"

static void
print_elapsed(uint ticks)
{
  uint seconds;
  uint hundredths;

  seconds = ticks / 100;
  hundredths = ticks % 100;
  printf(2, "real %d.%02ds\n", seconds, hundredths);
}

int
main(int argc, char *argv[])
{
  int pid;
  int status;
  uint start;
  uint end;

  if(argc < 2){
    printf(2, "usage: time command [args...]\n");
    exit();
  }

  start = (uint)uptime();
  pid = fork();
  if(pid < 0){
    printf(2, "time: fork failed\n");
    exit();
  }

  if(pid == 0){
    exec(argv[1], &argv[1]);
    printf(2, "time: exec %s failed\n", argv[1]);
    exit();
  }

  status = 0;
  if(waitpid(pid, &status, 0) < 0){
    printf(2, "time: waitpid failed\n");
    exit();
  }

  end = (uint)uptime();
  if(end < start)
    end = start;
  print_elapsed(end - start);
  exit();
}
