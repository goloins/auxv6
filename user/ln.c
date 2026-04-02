#include "types.h"
#include "stat.h"
#include "auxv6/user.h"

int
main(int argc, char *argv[])
{
  int is_symlink = 0;
  int i;
  char *old;
  char *new;

  // Parse arguments
  i = 1;
  if(i < argc && argv[i][0] == '-'){
    if(argv[i][1] == 's' && argv[i][2] == 0){
      is_symlink = 1;
      i++;
    } else {
      printf(2, "Usage: ln [-s] old new\n");
      exit();
    }
  }

  if(i + 2 != argc){
    printf(2, "Usage: ln [-s] old new\n");
    exit();
  }

  old = argv[i];
  new = argv[i + 1];

  if(is_symlink){
    if(symlink(old, new) < 0)
      printf(2, "symlink %s %s: failed\n", old, new);
  } else {
    if(link(old, new) < 0)
      printf(2, "link %s %s: failed\n", old, new);
  }
  exit();
}
