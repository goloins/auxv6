// Test that fork fails gracefully.
// Tiny executable so that the limit can be filling the proc table.

#include "types.h"
#include "stat.h"
#include "auxv6/user.h"

#define N  1000

static void
tprintf(int fd, const char *s, ...)
{
  write(fd, s, strlen(s));
}

void
forktest(void)
{
  int n, pid;

  tprintf(1, "fork test\n");

  for(n=0; n<N; n++){
    pid = fork();
    if(pid < 0)
      break;
    if(pid == 0)
      exit(0);
  }

  if(n == N){
    tprintf(1, "fork claimed to work N times!\n", N);
    exit(0);
  }

  for(; n > 0; n--){
    if(wait() < 0){
      tprintf(1, "wait stopped early\n");
      exit(0);
    }
  }

  if(wait() != -1){
    tprintf(1, "wait got too many\n");
    exit(0);
  }

  tprintf(1, "fork test OK\n");
}

int
main(void)
{
  forktest();
  exit(0);
}
