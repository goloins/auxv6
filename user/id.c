#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"
#include "../include/fcntl.h"

#define BUF_MAX 1024
#define NAME_MAX 32

static void
copy_field(char *dst, int dstsz, char *src, int len)
{
  int i;

  if(dstsz <= 0)
    return;
  if(len >= dstsz)
    len = dstsz - 1;
  for(i = 0; i < len; i++)
    dst[i] = src[i];
  dst[len] = 0;
}

static int
lookup_user_by_uid(int uid, char *name, int namesz, int *gid_out)
{
  int fd;
  int n;
  int i;
  char buf[BUF_MAX];

  fd = open("/etc/passwd", O_RDONLY);
  if(fd < 0)
    return -1;
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0)
    return -1;
  buf[n] = 0;

  i = 0;
  while(i < n) {
    int j;
    int fstart[8];
    int flen[8];
    int nf;
    int uidval;
    int gidval;

    nf = 0;
    fstart[0] = i;
    for(j = i; j <= n; j++) {
      if(buf[j] == ':' || buf[j] == '\n' || buf[j] == 0) {
        if(nf < 8) {
          flen[nf] = j - fstart[nf];
          nf++;
        }
        if(buf[j] == '\n' || buf[j] == 0) {
          i = j + 1;
          break;
        }
        if(nf < 8)
          fstart[nf] = j + 1;
      }
    }

    if(nf < 4)
      continue;

    uidval = 0;
    for(j = 0; j < flen[2]; j++) {
      char c = buf[fstart[2] + j];
      if(c < '0' || c > '9') {
        uidval = -1;
        break;
      }
      uidval = uidval * 10 + (c - '0');
    }
    if(uidval != uid)
      continue;

    gidval = 0;
    for(j = 0; j < flen[3]; j++) {
      char c = buf[fstart[3] + j];
      if(c < '0' || c > '9') {
        gidval = -1;
        break;
      }
      gidval = gidval * 10 + (c - '0');
    }

    copy_field(name, namesz, buf + fstart[0], flen[0]);
    if(gid_out)
      *gid_out = gidval;
    return 0;
  }

  return -1;
}

int
main(void)
{
  int uid;
  int gid;
  int pgid;
  int passwd_gid;
  char user[NAME_MAX];

  uid = getuid();
  gid = getgid();
  pgid = getpgrp();
  user[0] = 0;
  passwd_gid = -1;

  if(lookup_user_by_uid(uid, user, sizeof(user), &passwd_gid) < 0)
    strcpy(user, "unknown");

  printf(1, "uid=%d(%s) gid=%d", uid, user, gid);
  if(passwd_gid >= 0 && passwd_gid != gid)
    printf(1, " passwd_gid=%d", passwd_gid);
  printf(1, " pgrp=%d\n", pgid);

  exit();
}
