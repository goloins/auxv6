#include "types.h"
#include "pwd.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"

#define USER_MAX 32
#define MOTD_MAX 2048
#define HOST_MAX 64

/* ---------- MOTD helpers ---------- */

static void trim_trailing_ws(char *s);  /* defined below */

static int
motd_read_file(const char *path, char *buf, int max)
{
  int fd, n, off;
  if(max <= 0) return -1;
  fd = open(path, O_RDONLY);
  if(fd < 0) return -1;
  off = 0;
  while(off < max - 1){
    n = read(fd, buf + off, max - 1 - off);
    if(n <= 0) break;
    off += n;
  }
  close(fd);
  buf[off] = 0;
  return off;
}

static int
motd_find_kb(char *text, const char *key)
{
  int i, j;
  for(i = 0; text[i]; i++){
    for(j = 0; key[j] && text[i+j] == key[j]; j++)
      ;
    if(!key[j]){
      i += j;
      while(text[i] && (text[i] < '0' || text[i] > '9')) i++;
      return atoi(&text[i]);
    }
  }
  return -1;
}

static int
motd_next_tok(char **pp, char *tok, int max)
{
  char *p = *pp;
  int n;
  while(*p == ' ' || *p == '\t') p++;
  if(!*p || *p == '\n') return 0;
  n = 0;
  while(*p && *p != ' ' && *p != '\t' && *p != '\n'){
    if(n < max-1) tok[n++] = *p;
    p++;
  }
  tok[n] = 0;
  *pp = p;
  return 1;
}

static void
motd_humanize(uint bytes, char *buf, int bufsz)
{
  static const char units[] = "BKMG";
  uint div = 1;
  int idx = 0;
  uint val;
  char tmp[16];
  int i, j;
  while(idx < 3 && bytes / div >= 1024){ div *= 1024; idx++; }
  val = bytes / div;
  i = 0;
  if(val == 0){ tmp[i++] = '0'; }
  else{ while(val > 0){ tmp[i++] = '0' + (val % 10); val /= 10; } }
  j = 0;
  while(i > 0 && j < bufsz - 2) buf[j++] = tmp[--i];
  if(j < bufsz - 1) buf[j++] = units[idx];
  buf[j] = 0;
}

static void
motd_humanize_kb(int kb, char *buf, int bufsz)
{
  motd_humanize((uint)kb * 1024, buf, bufsz);
}

static void
read_hostname(char *buf, int bufsz)
{
  if(motd_read_file("/etc/hostname", buf, bufsz) < 0 || buf[0] == 0)
    strcpy(buf, "auxv6");
  else
    trim_trailing_ws(buf);
}

static void
read_free_mem(char *buf, int bufsz)
{
  static char text[256];
  int fkb;
  if(motd_read_file("/proc/meminfo", text, sizeof(text)) < 0){ strcpy(buf, "n/a"); return; }
  fkb = motd_find_kb(text, "MemFree:");
  if(fkb < 0){ strcpy(buf, "n/a"); return; }
  motd_humanize_kb(fkb, buf, bufsz);
}

static void
read_root_free_disk(char *buf, int bufsz)
{
  static char text[1024];
  char *p, *line;
  static char tok[6][32];
  int t;

  if(bufsz > 0)
    buf[0] = 0;

  if(motd_read_file("/proc/mountstats", text, sizeof(text)) < 0){ strcpy(buf, "n/a"); return; }
  p = text;
  while(*p){
    line = p;
    while(*p && *p != '\n') p++;
    if(*p == '\n') *p++ = 0;
    if(line[0] == 0) continue;
    { char *nl = line;
      for(t = 0; t < 6; t++) if(!motd_next_tok(&nl, tok[t], 32)) break;
      if(t < 6) continue;
      if(strcmp(tok[0], "dev") == 0 && strcmp(tok[1], "path") == 0)
        continue;
    }
    if(strcmp(tok[1], "/") != 0) continue;
    {
      int fblk = atoi(tok[4]);
      int bsz  = atoi(tok[5]);
      if(fblk < 0 || bsz <= 0){ strcpy(buf, "n/a"); return; }
      motd_humanize((uint)fblk * (uint)bsz, buf, bufsz);
      return;
    }
  }
  strcpy(buf, "n/a");
}

static int
motd_safe_len(const char *s, int max)
{
  int n;

  if(!s || max <= 0)
    return 0;

  n = 0;
  while(n < max && s[n] && s[n] != '\n' && s[n] != '\r')
    n++;
  return n;
}

static void
print_motd(void)
{
  static char motd[MOTD_MAX];
  static char hostname[HOST_MAX];
  static char free_mem[16];
  static char free_disk[16];
  const char *p;
  const char *tok_h  = "@HOSTNAME@";
  const char *tok_m  = "@FREE_MEM@";
  const char *tok_d  = "@FREE_DISK@";
  int lh, lm, ld;
  int hlen, mlen, dlen;

  if(motd_read_file("/etc/motd", motd, sizeof(motd)) < 0)
    return;

  read_hostname(hostname, sizeof(hostname));
  read_free_mem(free_mem, sizeof(free_mem));
  read_root_free_disk(free_disk, sizeof(free_disk));

  lh = strlen(tok_h);
  lm = strlen(tok_m);
  ld = strlen(tok_d);
  hlen = motd_safe_len(hostname, sizeof(hostname) - 1);
  mlen = motd_safe_len(free_mem, sizeof(free_mem) - 1);
  dlen = motd_safe_len(free_disk, sizeof(free_disk) - 1);

  for(p = motd; *p; ){
    if(strncmp(p, tok_h, lh) == 0){ write(1, hostname,  hlen); p += lh; }
    else if(strncmp(p, tok_m, lm) == 0){ write(1, free_mem,  mlen); p += lm; }
    else if(strncmp(p, tok_d, ld) == 0){ write(1, free_disk, dlen); p += ld; }
    else { write(1, p, 1); p++; }
  }
}

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

int
main(int argc, char *argv[])
{
  int fd;
  char user[USER_MAX];
  char pass[USER_MAX];
  struct passwd *ent;
  char *sh_argv[2];

  (void)argc;
  (void)argv;

  while((fd = open("/dev/console", O_RDWR)) >= 0) {
    if(fd >= 3) {
      close(fd);
      break;
    }
  }

  for(;;) {
    dprintf(1, "login: ");
    memset(user, 0, sizeof(user));
    if(gets(user, sizeof(user)) == 0)
      exit(0);

    trim_trailing_ws(user);
    if(user[0] == 0)
      continue;

    ent = getpwnam(user);
    if(ent == 0) {
      dprintf(1, "login: unknown user %s\n", user);
      continue;
    }

    dprintf(1, "password: ");
    memset(pass, 0, sizeof(pass));
    if(readpass(pass, sizeof(pass)) == 0)
      exit(0);
    trim_trailing_ws(pass);

    if(strcmp(pass, ent->pw_passwd) != 0) {
      dprintf(1, "login: authentication failed\n");
      continue;
    }

    if(setgid(ent->pw_gid) < 0 || setuid(ent->pw_uid) < 0) {
      dprintf(1, "login: permission denied\n");
      continue;
    }

    print_motd();

    if(ent->pw_dir[0] != 0)
      chdir(ent->pw_dir);

    sh_argv[0] = (ent->pw_shell && ent->pw_shell[0]) ? ent->pw_shell : "/bin/sh";
    sh_argv[1] = 0;
    exec(sh_argv[0], sh_argv);

    dprintf(1, "login: exec %s failed\n", sh_argv[0]);
  }
}
