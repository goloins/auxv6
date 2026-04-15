#include "auxv6/user.h"

int
main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  dprintf(2, "This account is currently not available.\n");
  return 1;
}
