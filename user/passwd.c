#include "types.h"
#include "pwd.h"
#include "sys/stat.h"
#include "auxv6/user.h"
#include "fcntl.h"

#define USER_MAX 32
#define PASSWD_BUF_MAX 4096

static void
trim_trailing_ws(char *s)
{
  int n;

  n = strlen(s);
  while(n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                  s[n - 1] == '\r' || s[n - 1] == '\n'))
    n--;
  s[n] = 0;
}

static int
read_passwd(char *buf, int bufsz, int *n_out)
{
  int fd;
  int n;

  fd = open("/etc/passwd", O_RDONLY);
  if(fd < 0)
    return -1;

  n = read(fd, buf, bufsz - 1);
  close(fd);
  if(n < 0)
    return -1;
  buf[n] = 0;
  *n_out = n;
  return 0;
}

static int
append_bytes(char *dst, int *cur, int max, char *src, int n)
{
  int i;

  if(*cur + n >= max)
    return -1;
  for(i = 0; i < n; i++)
    dst[*cur + i] = src[i];
  *cur += n;
  return 0;
}

static int
update_password(const char *user, const char *newpass)
{
  int i;
  int n;
  int outn;
  int replaced;
  int fd;
  char inbuf[PASSWD_BUF_MAX];
  char outbuf[PASSWD_BUF_MAX];

  if(read_passwd(inbuf, sizeof(inbuf), &n) < 0)
    return -1;

  outn = 0;
  replaced = 0;
  i = 0;

  while(i <= n) {
    int j;
    int line_start;
    int line_end;
    int fstart[8];
    int flen[8];
    int nf;

    if(i == n)
      break;

    if(inbuf[i] == '\n' || inbuf[i] == '\r') {
      i++;
      continue;
    }

    line_start = i;
    while(i < n && inbuf[i] != '\n' && inbuf[i] != '\r')
      i++;
    line_end = i;
    i++;

    nf = 0;
    fstart[0] = line_start;
    for(j = line_start; j <= line_end; j++) {
      if(j == line_end || inbuf[j] == ':') {
        if(nf < 8) {
          flen[nf] = j - fstart[nf];
          nf++;
        }
        if(j < line_end && nf < 8)
          fstart[nf] = j + 1;
      }
    }

    if(nf >= 3 && flen[0] == (int)strlen(user) &&
       strncmp(inbuf + fstart[0], user, flen[0]) == 0) {
      if(append_bytes(outbuf, &outn, sizeof(outbuf), inbuf + fstart[0], flen[0]) < 0)
        return -1;
      if(append_bytes(outbuf, &outn, sizeof(outbuf), ":", 1) < 0)
        return -1;
      if(append_bytes(outbuf, &outn, sizeof(outbuf), (char*)newpass, strlen(newpass)) < 0)
        return -1;
      if(append_bytes(outbuf, &outn, sizeof(outbuf), ":", 1) < 0)
        return -1;
      if(append_bytes(outbuf, &outn, sizeof(outbuf), inbuf + fstart[2], line_end - fstart[2]) < 0)
        return -1;
      if(append_bytes(outbuf, &outn, sizeof(outbuf), "\n", 1) < 0)
        return -1;
      replaced = 1;
    } else {
      if(append_bytes(outbuf, &outn, sizeof(outbuf), inbuf + line_start, line_end - line_start) < 0)
        return -1;
      if(append_bytes(outbuf, &outn, sizeof(outbuf), "\n", 1) < 0)
        return -1;
    }
  }

  if(!replaced)
    return -1;

  unlink("/etc/passwd.tmp");
  fd = open("/etc/passwd.tmp", O_CREATE | O_WRONLY);
  if(fd < 0)
    return -1;
  if(write(fd, outbuf, outn) != outn) {
    close(fd);
    return -1;
  }
  close(fd);

  if(unlink("/etc/passwd") < 0)
    return -1;
  if(link("/etc/passwd.tmp", "/etc/passwd") < 0)
    return -1;
  unlink("/etc/passwd.tmp");

  return 0;
}

int
main(int argc, char *argv[])
{
  int uid;
  struct passwd *cur;
  struct passwd *target;
  char oldpw[USER_MAX];
  char newpw[USER_MAX];
  char confpw[USER_MAX];
  char *target_name;

  if(argc > 2) {
    dprintf(2, "usage: passwd [user]\n");
    exit(0);
  }

  uid = getuid();
  if(uid < 0)
    uid = 0;

  cur = getpwuid((uid_t)uid);
  if(cur == 0) {
    dprintf(2, "passwd: cannot resolve current user\n");
    exit(0);
  }

  target_name = (argc == 2) ? argv[1] : cur->pw_name;
  target = getpwnam(target_name);
  if(target == 0) {
    dprintf(2, "passwd: unknown user %s\n", target_name);
    exit(0);
  }

  if(uid != 0 && strcmp(cur->pw_name, target->pw_name) != 0) {
    dprintf(2, "passwd: permission denied\n");
    exit(0);
  }

  if(uid != 0) {
    dprintf(1, "Current password: ");
    memset(oldpw, 0, sizeof(oldpw));
    if(readpass(oldpw, sizeof(oldpw)) == 0)
      exit(0);
    trim_trailing_ws(oldpw);

    if(strcmp(oldpw, target->pw_passwd) != 0) {
      dprintf(2, "passwd: authentication failed\n");
      exit(0);
    }
  }

  dprintf(1, "New password: ");
  memset(newpw, 0, sizeof(newpw));
  if(readpass(newpw, sizeof(newpw)) == 0)
    exit(0);
  trim_trailing_ws(newpw);

  dprintf(1, "Retype new password: ");
  memset(confpw, 0, sizeof(confpw));
  if(readpass(confpw, sizeof(confpw)) == 0)
    exit(0);
  trim_trailing_ws(confpw);

  if(newpw[0] == 0) {
    dprintf(2, "passwd: empty password not allowed\n");
    exit(0);
  }
  if(strchr(newpw, ':') || strchr(newpw, '\n') || strchr(newpw, '\r')) {
    dprintf(2, "passwd: invalid characters in password\n");
    exit(0);
  }
  if(strcmp(newpw, confpw) != 0) {
    dprintf(2, "passwd: passwords do not match\n");
    exit(0);
  }

  if(update_password(target->pw_name, newpw) < 0) {
    dprintf(2, "passwd: failed to update /etc/passwd\n");
    exit(0);
  }

  dprintf(1, "passwd: password updated for %s\n", target->pw_name);
  exit(0);
}
