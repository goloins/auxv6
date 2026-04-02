#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"

#define UNAME_MAX 32
#define HOST_MAX 64

static void
trim_hostname(char *buf)
{
  int i;

  for(i = 0; buf[i]; i++){
    if(buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ' || buf[i] == '\t'){
      buf[i] = 0;
      return;
    }
  }
}

static int
load_hostname(char *buf, int bufsz)
{
  int fd;
  int n;

  fd = open("/etc/hostname", O_RDONLY);
  if(fd < 0)
    return -1;
  n = read(fd, buf, bufsz - 1);
  close(fd);
  if(n <= 0)
    return -1;
  buf[n] = 0;
  trim_hostname(buf);
  return buf[0] ? 0 : -1;
}

int
main(int argc, char *argv[])
{
  char release[UNAME_MAX];
  char host[HOST_MAX];

  if(argc != 1){
    dprintf(2, "usage: uname\n");
    exit(0);
  }

  if(uname(release, sizeof(release)) < 0){
    dprintf(2, "uname: syscall failed\n");
    exit(0);
  }

  if(load_hostname(host, sizeof(host)) < 0)
    dprintf(1, "%s\n", release);
  else
    dprintf(1, "%s %s\n", release, host);

  exit(0);
}