#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"

#define USER_MAX 32
#define PATH_MAX 64
#define MOTD_MAX 2048
#define HOST_MAX 64

struct passwd_entry {
  char user[USER_MAX];
  char pass[USER_MAX];
  int uid;
  int gid;
  char home[PATH_MAX];
  char shell[PATH_MAX];
};

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
  static char text[512];
  char *p, *line;
  static char tok[6][32];
  int t;
  if(motd_read_file("/proc/mountstats", text, sizeof(text)) < 0){ strcpy(buf, "n/a"); return; }
  p = text;
  while(*p){
    line = p;
    while(*p && *p != '\n') p++;
    if(*p == '\n') *p++ = 0;
    if(line[0] == 0 || line[0] == 'd') continue;
    { char *nl = line;
      for(t = 0; t < 6; t++) if(!motd_next_tok(&nl, tok[t], 32)) break;
      if(t < 6) continue;
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

  if(motd_read_file("/etc/motd", motd, sizeof(motd)) < 0)
    return;

  read_hostname(hostname, sizeof(hostname));
  read_free_mem(free_mem, sizeof(free_mem));
  read_root_free_disk(free_disk, sizeof(free_disk));

  lh = strlen(tok_h);
  lm = strlen(tok_m);
  ld = strlen(tok_d);

  for(p = motd; *p; ){
    if(strncmp(p, tok_h, lh) == 0){ write(1, hostname,  strlen(hostname));  p += lh; }
    else if(strncmp(p, tok_m, lm) == 0){ write(1, free_mem,  strlen(free_mem));  p += lm; }
    else if(strncmp(p, tok_d, ld) == 0){ write(1, free_disk, strlen(free_disk)); p += ld; }
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
    int gid;

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

    gid = 0;
    for(j = 0; j < flen[3]; j++) {
      char c;

      c = buf[fstart[3] + j];
      if(c < '0' || c > '9')
        return -1;
      gid = gid * 10 + (c - '0');
    }

    if(entry != 0) {
      copy_field(entry->user, sizeof(entry->user), buf + fstart[0], flen[0]);
      copy_field(entry->pass, sizeof(entry->pass), buf + fstart[1], flen[1]);
      entry->uid = uid;
      entry->gid = gid;
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
  int fd;
  char user[USER_MAX];
  char pass[USER_MAX];
  struct passwd_entry ent;
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

    if(lookup_user(user, &ent) < 0) {
      dprintf(1, "login: unknown user %s\n", user);
      continue;
    }

    dprintf(1, "password: ");
    memset(pass, 0, sizeof(pass));
    if(readpass(pass, sizeof(pass)) == 0)
      exit(0);
    trim_trailing_ws(pass);

    if(strcmp(pass, ent.pass) != 0) {
      dprintf(1, "login: authentication failed\n");
      continue;
    }

    if(setgid(ent.gid) < 0 || setuid(ent.uid) < 0) {
      dprintf(1, "login: permission denied\n");
      continue;
    }

    print_motd();

    if(ent.home[0] != 0)
      chdir(ent.home);

    sh_argv[0] = ent.shell;
    sh_argv[1] = 0;
    exec(ent.shell, sh_argv);

    dprintf(1, "login: exec %s failed\n", ent.shell);
  }
}
