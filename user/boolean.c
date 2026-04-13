#include "auxv6/user.h"
#include "string.h"

int
main(int argc, char *argv[])
{
  const char *p;

  (void)argc;

  p = argv[0];
  while(*p)
    p++;
  while(p > argv[0] && p[-1] != '/')
    p--;

  if(strcmp(p, "false") == 0)
    return 1;
  return 0;
}
