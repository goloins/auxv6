#include "types.h"
#include "stat.h"
#include "auxv6/user.h"

#define DMESG_CHUNK 4096

int
main(int argc, char *argv[])
{
  static char buf[DMESG_CHUNK];
  int n;

  if(argc > 1) {
    printf(2, "usage: dmesg\n");
    exit();
  }

  n = kmsgread(buf, sizeof(buf));
  if(n < 0) {
    printf(2, "dmesg: kmsgread failed\n");
    exit();
  }

  if(n > 0)
    write(1, buf, n);

  exit();
}
