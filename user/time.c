#include "types.h"
#include "auxv6/user.h"

static void
print_elapsed(uint ticks)
{
  uint seconds;
  uint hundredths;

  seconds = ticks / 100;
  hundredths = ticks % 100;
  dprintf(2, "real %d.%02ds\n", seconds, hundredths);
}

int
main(int argc, char *argv[])
{
  int pid;
  int status;
  uint start;
  uint end;

  if(argc < 2){
    dprintf(2, "usage: time command [args...]\n");
    exit(1);
  }

  start = (uint)uptime();
  pid = fork();
  if(pid < 0){
    dprintf(2, "time: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    exec(argv[1], &argv[1]);
    dprintf(2, "time: exec %s failed\n", argv[1]);
    exit(1);
  }

  status = 0;
  if(waitpid(pid, &status, 0) < 0){
    dprintf(2, "time: waitpid failed\n");
    exit(1);
  }

  end = (uint)uptime();
  if(end < start)
    end = start;
  print_elapsed(end - start);
  exit(0);
}
