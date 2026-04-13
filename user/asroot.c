#include "pwd.h"
#include "auxv6/user.h"

int
main(int argc, char *argv[])
{
  if(argc < 2) {
    dprintf(2, "usage: asroot command [args ...]\n");
    return 1;
  }

  if(getuid() != 0) {
    dprintf(2, "asroot: this minimal implementation requires a root shell\n");
    return 1;
  }

  exec(argv[1], &argv[1]);
  dprintf(2, "asroot: exec %s failed\n", argv[1]);
  return 1;
}
