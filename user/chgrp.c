#include "types.h"
#include "grp.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "auxv6/user.h"

static int
parse_decimal_prefix(const char *s, int len)
{
  int i;
  int value;

  if(len <= 0)
    return -1;
  value = 0;
  for(i = 0; i < len; i++){
    if(s[i] < '0' || s[i] > '9')
      return -1;
    value = value * 10 + (s[i] - '0');
  }
  return value;
}

static int
parse_decimal(const char *s)
{
  return parse_decimal_prefix(s, strlen(s));
}

int
main(int argc, char *argv[])
{
  int i;
  int gid;
  struct group *gr;

  if(argc < 3){
    dprintf(2, "usage: chgrp group file...\n");
    exit(0);
  }

  gid = parse_decimal(argv[1]);
  if(gid < 0) {
    gr = getgrnam(argv[1]);
    if(gr != 0)
      gid = gr->gr_gid;
  }
  if(gid < 0){
    dprintf(2, "chgrp: unknown group %s\n", argv[1]);
    exit(0);
  }

  for(i = 2; i < argc; i++){
    if(chown(argv[i], -1, gid) < 0){
      dprintf(2, "chgrp: %s failed\n", argv[i]);
      break;
    }
  }

  exit(0);
}