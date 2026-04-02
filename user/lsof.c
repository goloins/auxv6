#include "types.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define LSOF_PATH "/proc/lsof"

static int
line_pid(char *line)
{
  int pid;
  int i;

  pid = 0;
  i = 0;
  while(line[i] == ' ' || line[i] == '\t')
    i++;
  if(line[i] < '0' || line[i] > '9')
    return -1;
  while(line[i] >= '0' && line[i] <= '9'){
    pid = pid * 10 + (line[i] - '0');
    i++;
  }
  return pid;
}

int
main(int argc, char *argv[])
{
  int fd;
  int n;
  int filter_pid;
  int have_filter;
  char buf[512];
  char line[256];
  int llen;
  int i;

  have_filter = 0;
  filter_pid = 0;
  if(argc > 2){
    printf(2, "usage: lsof [pid]\n");
    exit();
  }
  if(argc == 2){
    have_filter = 1;
    filter_pid = atoi(argv[1]);
    if(filter_pid <= 0){
      printf(2, "lsof: invalid pid %s\n", argv[1]);
      exit();
    }
  }

  fd = open(LSOF_PATH, O_RDONLY);
  if(fd < 0){
    printf(2, "lsof: cannot open %s\n", LSOF_PATH);
    exit();
  }

  llen = 0;
  while((n = read(fd, buf, sizeof(buf))) > 0){
    for(i = 0; i < n; i++){
      char c = buf[i];
      if(llen < (int)sizeof(line) - 1)
        line[llen++] = c;
      if(c != '\n')
        continue;

      line[llen] = 0;
      if(!have_filter){
        write(1, line, llen);
      } else {
        int pid = line_pid(line);
        if(pid < 0 || pid == filter_pid)
          write(1, line, llen);
      }
      llen = 0;
    }
  }

  close(fd);
  if(n < 0){
    printf(2, "lsof: read error\n");
    exit();
  }
  exit();
}
