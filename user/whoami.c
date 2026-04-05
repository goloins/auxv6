#include "types.h"
#include "pwd.h"
#include "stat.h"
#include "stdio.h"
#include "auxv6/user.h"
#include "fcntl.h"

#define WHOAMI_NAME_BUFSZ 32

int
main(void)
{
  char name[WHOAMI_NAME_BUFSZ];
  struct passwd *pw;

  pw = getpwuid((uid_t)getuid());
  if(pw == 0)
    strcpy(name, "unknown");
  else
    snprintf(name, sizeof(name), "%s", pw->pw_name);
  dprintf(1, "%s\n", name);
  exit(0);
}
