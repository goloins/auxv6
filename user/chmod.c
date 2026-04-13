#include "types.h"
#include "stat.h"
#include "auxv6/user.h"

static int
parse_mode(const char *s)
{
  int mode;

  if(s == 0 || *s == 0)
    return -1;

  mode = 0;
  while(*s){
    if(*s < '0' || *s > '7')
      return -1;
    mode = (mode << 3) + (*s - '0');
    s++;
  }
  return mode;
}

int
main(int argc, char *argv[])
{
  int i;
  int mode;
  int status;

  status = 0;

  if(argc < 3){
    dprintf(2, "usage: chmod mode file...\n");
    exit(1);
  }

  mode = parse_mode(argv[1]);
  if(mode < 0){
    dprintf(2, "chmod: invalid mode %s\n", argv[1]);
    exit(1);
  }

  for(i = 2; i < argc; i++){
    if(chmod(argv[i], mode) < 0){
      dprintf(2, "chmod: %s failed\n", argv[i]);
      status = 1;
      break;
    }
  }

  exit(status);
}