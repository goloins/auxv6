#include "auxv6/user.h"
#include "stdlib.h"

int
main(int argc, char *argv[])
{
  int i;
  uint total;

  if(argc < 2) {
    dprintf(2, "usage: sleep seconds...\n");
    return 1;
  }

  total = 0;
  for(i = 1; i < argc; i++) {
    char *end;
    long v;

    v = strtol(argv[i], &end, 10);
    if(*argv[i] == 0 || *end != 0 || v < 0) {
      dprintf(2, "sleep: invalid duration: %s\n", argv[i]);
      return 1;
    }
    total += (uint)v;
  }

  sleep(total);
  return 0;
}
