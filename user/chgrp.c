#include "../include/types.h"
#include "../include/stat.h"
#include "../include/fcntl.h"
#include "../include/user.h"

#define PASSWD_BUF_MAX 2048

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

static int
lookup_gid(const char *name)
{
  int fd;
  int n;
  int i;
  char buf[PASSWD_BUF_MAX];

  fd = open("/etc/passwd", O_RDONLY);
  if(fd < 0)
    return -1;
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0)
    return -1;
  buf[n] = 0;

  i = 0;
  while(i < n){
    int j;
    int fstart[8];
    int flen[8];
    int nf;

    nf = 0;
    fstart[0] = i;
    for(j = i; j <= n; j++){
      if(buf[j] == ':' || buf[j] == '\n' || buf[j] == 0){
        if(nf < 8){
          flen[nf] = j - fstart[nf];
          nf++;
        }
        if(buf[j] == '\n' || buf[j] == 0){
          i = j + 1;
          break;
        }
        if(nf < 8)
          fstart[nf] = j + 1;
      }
    }
    if(nf < 4)
      continue;
    if(flen[0] != (int)strlen(name))
      continue;
    if(strncmp(name, buf + fstart[0], flen[0]) != 0)
      continue;
    return parse_decimal_prefix(buf + fstart[3], flen[3]);
  }

  return -1;
}

int
main(int argc, char *argv[])
{
  int i;
  int gid;

  if(argc < 3){
    printf(2, "usage: chgrp group file...\n");
    exit();
  }

  gid = parse_decimal(argv[1]);
  if(gid < 0)
    gid = lookup_gid(argv[1]);
  if(gid < 0){
    printf(2, "chgrp: unknown group %s\n", argv[1]);
    exit();
  }

  for(i = 2; i < argc; i++){
    if(chown(argv[i], -1, gid) < 0){
      printf(2, "chgrp: %s failed\n", argv[i]);
      break;
    }
  }

  exit();
}