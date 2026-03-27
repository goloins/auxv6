#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"

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
      printf(1, "cat: write error\n");
      exit();
    }
  }
  if(n < 0){
    printf(1, "cat: read error\n");
    exit();
  }

  if(ensure_newline && saw_data && last != '\n') {
    if(write(1, "\n", 1) != 1) {
      printf(1, "cat: write error\n");
      exit();
    }
  }
}

int
main(int argc, char *argv[])
{
  int fd, i;

  if(argc <= 1){
    cat(0, 0);
    exit();
  }

  for(i = 1; i < argc; i++){
    if((fd = open(argv[i], 0)) < 0){
      printf(1, "cat: cannot open %s\n", argv[i]);
      exit();
    }
    cat(fd, 1);
    close(fd);
  }
  exit();
}
