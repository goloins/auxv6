#include "auxv6/user.h"

int
main(int argc, char *argv[])
{
  char line[256];
  int len;
  int i;

  if(argc <= 1) {
    line[0] = 'y';
    line[1] = '\n';
    len = 2;
  } else {
    len = 0;
    for(i = 1; i < argc; i++) {
      int j;
      if(i > 1 && len < (int)sizeof(line) - 1)
        line[len++] = ' ';
      for(j = 0; argv[i][j] && len < (int)sizeof(line) - 1; j++)
        line[len++] = argv[i][j];
    }
    if(len >= (int)sizeof(line) - 1)
      len = (int)sizeof(line) - 2;
    line[len++] = '\n';
  }

  while(1) {
    if(write(1, line, len) < 0)
      return 1;
  }
}
