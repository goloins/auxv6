#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define PASSWD_BUF_MAX 2048
#define NAME_MAX 32

static int
parse_decimal(const char *s)
{
  int value;

  if(s == 0 || *s == 0)
    return -1;
  value = 0;
  while(*s){
    if(*s < '0' || *s > '9')
      return -1;
    value = value * 10 + (*s - '0');
    s++;
  }
  return value;
}

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
lookup_user(const char *name, int *uid_out, int *gid_out)
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
    int uid;
    int gid;

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

    uid = parse_decimal_prefix(buf + fstart[2], flen[2]);
    gid = parse_decimal_prefix(buf + fstart[3], flen[3]);
    if(uid < 0 || gid < 0)
      return -1;
    *uid_out = uid;
    *gid_out = gid;
    return 0;
  }

  return -1;
}

int
main(int argc, char *argv[])
{
  int i;
  int uid;
  int gid;

  if(argc < 3){
    dprintf(2, "usage: chown owner file...\n");
    exit(0);
  }

  uid = parse_decimal(argv[1]);
  gid = -1;
  if(uid < 0 && lookup_user(argv[1], &uid, &gid) < 0){
    dprintf(2, "chown: unknown owner %s\n", argv[1]);
    exit(0);
  }

  for(i = 2; i < argc; i++){
    if(chown(argv[i], uid, gid) < 0){
      dprintf(2, "chown: %s failed\n", argv[i]);
      break;
    }
  }

  exit(0);
}