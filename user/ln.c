#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"

int
main(int argc, char *argv[])
{
  int is_symlink = 0;
  int i;
  int status;
  char *old;
  char *new;

  status = 0;

  // Parse arguments
  i = 1;
  if(i < argc && argv[i][0] == '-'){
    if(argv[i][1] == 's' && argv[i][2] == 0){
      is_symlink = 1;
      i++;
    } else {
      dprintf(2, "Usage: ln [-s] old new\n");
      exit(1);
    }
  }

  if(i + 2 != argc){
    dprintf(2, "Usage: ln [-s] old new\n");
    exit(1);
  }

  old = argv[i];
  new = argv[i + 1];

  if(is_symlink){
    if(symlink(old, new) < 0){
      dprintf(2, "symlink %s %s: failed\n", old, new);
      status = 1;
    }
  } else {
    if(link(old, new) < 0){
      dprintf(2, "link %s %s: failed\n", old, new);
      status = 1;
    }
  }
  exit(status);
}
