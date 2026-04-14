#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"

#define DMESG_CHUNK 4096

int
main(int argc, char *argv[])
{
  static char buf[DMESG_CHUNK];
  int n;

  if(argc > 1) {
    dprintf(2, "usage: dmesg\n");
    exit(0);
  }

  n = kmsgread(buf, sizeof(buf));
  if(n < 0) {
    dprintf(2, "dmesg: kmsgread failed\n");
    exit(0);
  }

  if(n > 0)
    write(1, buf, n);

  exit(0);
}
