#include "types.h"
#include "auxv6/user.h"

int
main(int argc, char **argv)
{
  char *xargv[64];
  int i;

  xargv[0] = "/bin/xinit";
  for(i = 1; i < argc && i < (int)(sizeof(xargv) / sizeof(xargv[0])) - 1; i++)
    xargv[i] = argv[i];
  xargv[i] = 0;

  exec(xargv[0], xargv);
  dprintf(2, "startx: exec failed for /bin/xinit\n");
  return 1;
}
