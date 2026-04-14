#include "types.h"
#include "sys/stat.h"
#include "auxv6/user.h"

char buf[512];

void
cat(int fd, int ensure_newline)
{
  int n;
  int saw_data;
  char last;

  saw_data = 0;
  last = '\n';

  while((n = read(fd, buf, sizeof(buf))) > 0) {
    saw_data = 1;
    last = buf[n - 1];
    if (write(1, buf, n) != n) {
      dprintf(1, "cat: write error\n");
      exit(1);
    }
  }
  if(n < 0){
    dprintf(1, "cat: read error\n");
    exit(1);
  }

  if(ensure_newline && saw_data && last != '\n') {
    if(write(1, "\n", 1) != 1) {
      dprintf(1, "cat: write error\n");
      exit(1);
    }
  }
}

int
main(int argc, char *argv[])
{
  int fd, i;

  if(argc <= 1){
    cat(0, 0);
    exit(0);
  }

  for(i = 1; i < argc; i++){
    if((fd = open(argv[i], 0)) < 0){
      dprintf(1, "cat: cannot open %s\n", argv[i]);
      exit(1);
    }
    cat(fd, 1);
    close(fd);
  }
  exit(0);
}
