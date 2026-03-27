#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"
#include "../include/fcntl.h"

#define USER_MAX 32
#define PATH_MAX 64

struct passwd_entry {
  char user[USER_MAX];
  char pass[USER_MAX];
  int uid;
  char home[PATH_MAX];
  char shell[PATH_MAX];
};

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
lookup_user(const char *name, struct passwd_entry *entry)
{
  int fd;
  int n;
  int i;
  char buf[1024];

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
    int namelen;
    int uid;

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

    if(nf < 7)
      continue;

    namelen = strlen(name);
    if(flen[0] != namelen)
      continue;
    if(strncmp(name, buf + fstart[0], namelen) != 0)
      continue;

    uid = 0;
    for(j = 0; j < flen[2]; j++) {
      char c;

      c = buf[fstart[2] + j];
      if(c < '0' || c > '9')
        return -1;
      uid = uid * 10 + (c - '0');
    }

    if(entry != 0) {
      copy_field(entry->user, sizeof(entry->user), buf + fstart[0], flen[0]);
      copy_field(entry->pass, sizeof(entry->pass), buf + fstart[1], flen[1]);
      entry->uid = uid;
      copy_field(entry->home, sizeof(entry->home), buf + fstart[5], flen[5]);
      if(flen[6] > 0)
        copy_field(entry->shell, sizeof(entry->shell), buf + fstart[6], flen[6]);
      else
        copy_field(entry->shell, sizeof(entry->shell), "/bin/sh", strlen("/bin/sh"));
    }

    return 0;
  }

  return -1;
}

int
main(int argc, char *argv[])
{
  int uid;
  char pass[USER_MAX];
  char *target;
  char *sh_argv[2];
  struct passwd_entry ent;

  if(argc > 2) {
    printf(2, "usage: su [user]\n");
    exit();
  }

  target = (argc == 2) ? argv[1] : "root";
  if(lookup_user(target, &ent) < 0) {
    printf(2, "su: unknown user %s\n", target);
    exit();
  }

  uid = getuid();
  if(uid < 0)
    uid = 0;

  if(uid != 0) {
    printf(1, "Password: ");
    memset(pass, 0, sizeof(pass));
    if(gets(pass, sizeof(pass)) == 0)
      exit();
    trim_trailing_ws(pass);

    if(strcmp(pass, ent.pass) != 0) {
      printf(2, "su: authentication failed\n");
      exit();
    }
  }

  if(setuid(ent.uid) < 0) {
    printf(2, "su: permission denied\n");
    exit();
  }

  if(ent.home[0])
    chdir(ent.home);

  sh_argv[0] = ent.shell;
  sh_argv[1] = 0;
  exec(ent.shell, sh_argv);

  printf(2, "su: exec %s failed\n", ent.shell);
  exit();
}
