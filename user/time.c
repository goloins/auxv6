#include "types.h"
#include "time.h"
#include "auxv6/user.h"

static void
print_elapsed(unsigned long long elapsed_ms)
{
  unsigned long long seconds;
  unsigned long long milliseconds;

  seconds = elapsed_ms / 1000ULL;
  milliseconds = elapsed_ms % 1000ULL;
  dprintf(2, "real %llu.%03llus\n", seconds, milliseconds);
}

int
main(int argc, char *argv[])
{
  int pid;
  int status;
  struct timespec start;
  struct timespec end;

  if(argc < 2){
    dprintf(2, "usage: time command [args...]\n");
    exit(1);
  }

  if(clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
    dprintf(2, "time: clock_gettime failed\n");
    exit(1);
  }
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

  if(clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
    dprintf(2, "time: clock_gettime failed\n");
    exit(1);
  }
  print_elapsed(timespec_diff_msec(&start, &end));
  exit(0);
}
