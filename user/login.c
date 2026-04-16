#include "types.h"
#include "crypt.h"
#include "sys/stat.h"
#include "pwd.h"
#include "shadow.h"
#include "utmpx.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "stdlib.h"
#include "stdio.h"
#include "time.h"

#define LOGIN_NAME_MAX   64
#define LOGIN_PASS_MAX   64
#define LOGIN_PATH_MAX   128
#define LOGIN_BUF_MAX    4096
#define LOGIN_RETRIES    10
#define LOGIN_HOST_MAX   64

/* Toggle to 1 for built-in step tracing without build flags. */
#define LOGIN_DEBUG      0

#define NOLOGIN_PATH "/etc/nologin"
#define UTMP_PATH    "/var/run/utmp"
#define WTMP_PATH    "/var/log/wtmp"
#define MOTD_PATH    "/etc/motd"

mode_t umask(mode_t mask);

struct session_user {
  char name[LOGIN_NAME_MAX];
  char pass[LOGIN_PASS_MAX];
  int uid;
  int gid;
  char home[LOGIN_PATH_MAX];
  char shell[LOGIN_PATH_MAX];
};

static void
logdbg(const char *msg)
{
  if(!LOGIN_DEBUG)
    return;
  dprintf(1, "[login-dbg] %s\n", msg);
  dprintf(2, "[login-dbg] %s\n", msg);
}

static void
trim(char *s)
{
  int n;

  if(s == 0)
    return;

  n = strlen(s);
  while(n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                  s[n - 1] == ' '  || s[n - 1] == '\t'))
    n--;
  s[n] = 0;
}

static int
read_file_text(const char *path, char *buf, int bufsz)
{
  int fd;
  int n;

  if(path == 0 || buf == 0 || bufsz <= 1)
    return -1;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;

  n = read(fd, buf, bufsz - 1);
  close(fd);
  if(n < 0)
    return -1;

  buf[n] = 0;
  return n;
}

static void
copy_field(char *dst, int dstsz, const char *src, int len)
{
  if(dst == 0 || dstsz <= 0)
    return;
  if(src == 0 || len <= 0) {
    dst[0] = 0;
    return;
  }
  if(len >= dstsz)
    len = dstsz - 1;
  memmove(dst, src, len);
  dst[len] = 0;
}

static int
parse_uint(const char *s, int len, int *out)
{
  int i;
  int v;

  if(s == 0 || out == 0 || len <= 0)
    return -1;

  v = 0;
  for(i = 0; i < len; i++) {
    if(s[i] < '0' || s[i] > '9')
      return -1;
    v = v * 10 + (s[i] - '0');
  }

  *out = v;
  return 0;
}

static int
lookup_user_local(const char *name, struct session_user *u)
{
  static char buf[LOGIN_BUF_MAX];
  int n;
  int i;

  if(name == 0 || u == 0 || name[0] == 0)
    return -1;

  n = read_file_text("/etc/passwd", buf, sizeof(buf));
  if(n <= 0)
    return -1;

  i = 0;
  while(i < n) {
    int start;
    int end;
    int nf;
    int fs[8];
    int fl[8];
    int j;
    int uid;
    int gid;

    while(i < n && (buf[i] == '\n' || buf[i] == '\r'))
      i++;
    if(i >= n)
      break;

    start = i;
    while(i < n && buf[i] != '\n' && buf[i] != '\r')
      i++;
    end = i;

    nf = 0;
    fs[0] = start;
    for(j = start; j <= end; j++) {
      if(j == end || buf[j] == ':') {
        if(nf < 8) {
          fl[nf] = j - fs[nf];
          nf++;
        }
        if(j < end && nf < 8)
          fs[nf] = j + 1;
      }
    }

    if(nf < 7)
      continue;

    if(fl[0] != (int)strlen(name))
      continue;
    if(strncmp(buf + fs[0], name, fl[0]) != 0)
      continue;

    if(parse_uint(buf + fs[2], fl[2], &uid) < 0)
      return -1;
    if(parse_uint(buf + fs[3], fl[3], &gid) < 0)
      return -1;

    copy_field(u->name, sizeof(u->name), buf + fs[0], fl[0]);
    copy_field(u->pass, sizeof(u->pass), buf + fs[1], fl[1]);
    u->uid = uid;
    u->gid = gid;
    copy_field(u->home, sizeof(u->home), buf + fs[5], fl[5]);
    if(fl[6] > 0)
      copy_field(u->shell, sizeof(u->shell), buf + fs[6], fl[6]);
    else
      copy_field(u->shell, sizeof(u->shell), "/bin/sh", 7);

    return 0;
  }

  return -1;
}

static const char *
lookup_shadow_password(const char *name)
{
  struct spwd *sp;

  if(name == 0 || name[0] == 0)
    return 0;

  sp = getspnam(name);
  if(sp == 0 || sp->sp_pwdp == 0 || sp->sp_pwdp[0] == 0)
    return 0;

  if(strcmp(sp->sp_pwdp, "x") == 0 || strcmp(sp->sp_pwdp, "*") == 0 ||
     strcmp(sp->sp_pwdp, "!") == 0)
    return 0;
  return sp->sp_pwdp;
}

static int
is_locked_password(const char *stored)
{
  if(stored == 0)
    return 1;
  if(stored[0] == 0)
    return 0;
  return (strcmp(stored, "*") == 0 || strcmp(stored, "!") == 0 ||
          strcmp(stored, "x") == 0);
}

static int
verify_password(const char *input, const char *stored)
{
  char *calc;

  if(input == 0 || stored == 0)
    return 0;
  if(is_locked_password(stored))
    return 0;
  if(strncmp(stored, "$aux$", 5) == 0) {
    calc = crypt(input, stored);
    return (calc != 0 && strcmp(calc, stored) == 0);
  }
  return strcmp(input, stored) == 0;
}

static void
ensure_stdio(void)
{
  int fd;

  close(0);
  close(1);
  close(2);
  fd = open("/dev/console", O_RDWR);
  if(fd < 0)
    exit(0);

  if(fd != 0)
    dup2(fd, 0);
  dup(0);
  dup(0);

  if(fd > 2)
    close(fd);
}

static int
read_username(char *buf, int bufsz)
{
  int i;
  int cc;
  char c;

  if(buf == 0 || bufsz <= 1)
    return -1;

  i = 0;
  while(i + 1 < bufsz) {
    cc = read(0, &c, 1);
    if(cc < 1)
      return -1;
    if(c == '\n' || c == '\r')
      break;
    buf[i++] = c;
  }
  buf[i] = 0;
  return 0;
}

static void
restore_tty_cooked_mode(void)
{
  struct termios t;

  if(tcgetattr(0, &t) < 0)
    return;

  t.c_lflag |= (ECHO | ICANON);
  tcsetattr(0, TCSANOW, &t);
}

static void
claim_tty_foreground(void)
{
  int mypgrp;
  int fg_before;
  int fg_after;

  mypgrp = (int)getpid();
  setpgid(0, 0);
  fg_before = (int)tcgetpgrp();
  tcsetpgrp((pid_t)mypgrp);
  fg_after = (int)tcgetpgrp();

  if(LOGIN_DEBUG)
    dprintf(1, "[login-dbg] pgrp my=%d fg_before=%d fg_after=%d\n", mypgrp, fg_before, fg_after);
}

static void
show_nologin_and_exit_if_needed(const struct session_user *u)
{
  char buf[512];
  int fd;
  int n;

  if(u == 0 || u->uid == 0)
    return;

  fd = open(NOLOGIN_PATH, O_RDONLY);
  if(fd < 0)
    return;

  while((n = read(fd, buf, sizeof(buf))) > 0)
    write(1, buf, n);
  close(fd);
  exit(0);
}

static void
set_session_env(const struct session_user *u)
{
  char ttybuf[LOGIN_PATH_MAX];
  char *tty;

  clearenv();
  setenv("LOGNAME", u->name, 1);
  setenv("USER", u->name, 1);
  setenv("HOME", u->home, 1);
  setenv("SHELL", u->shell, 1);
  setenv("PATH", "/bin:/usr/bin:/sbin:/usr/sbin", 1);

  tty = ttyname(0);
  if(tty)
    setenv("TTY", tty, 1);
  else if(ttyname_r(0, ttybuf, sizeof(ttybuf)) == 0)
    setenv("TTY", ttybuf, 1);
}

static void
ensure_log_dirs(void)
{
  mkdir("/var");
  mkdir("/var/run");
  mkdir("/var/log");
}

static void
record_utmp_wtmp(const struct session_user *u)
{
  struct utmpx utx;
  char ttybuf[LOGIN_PATH_MAX];
  char *tty;

  if(u == 0)
    return;

  ensure_log_dirs();

  tty = ttyname(0);
  if(tty == 0 && ttyname_r(0, ttybuf, sizeof(ttybuf)) == 0)
    tty = ttybuf;

  memset(&utx, 0, sizeof(utx));
  utx.ut_type = USER_PROCESS;
  utx.ut_pid = getpid();
  utx.ut_tv.tv_sec = time(0);
  utx.ut_tv.tv_usec = 0;

  if(tty) {
    char *line;

    line = tty;
    if(strncmp(line, "/dev/", 5) == 0)
      line += 5;
    snprintf(utx.ut_line, sizeof(utx.ut_line), "%s", line);
  }

  snprintf(utx.ut_user, sizeof(utx.ut_user), "%s", u->name);
  utx.ut_id[0] = utx.ut_line[0];
  utx.ut_id[1] = utx.ut_line[1];
  utx.ut_id[2] = utx.ut_line[2];
  utx.ut_id[3] = utx.ut_line[3];

  pututxline(&utx);
  endutxent();
  updwtmpx(WTMP_PATH, &utx);
  write_lastlog((uid_t)u->uid, utx.ut_line, utx.ut_host, utx.ut_tv.tv_sec);
}

static int
read_memfree_kb(void)
{
  char buf[512];
  char *p;

  if(read_file_text("/proc/meminfo", buf, sizeof(buf)) < 0)
    return -1;

  p = buf;
  while(*p) {
    if(strncmp(p, "MemFree:", 8) == 0)
      break;
    p++;
  }
  if(*p == 0)
    return -1;

  while(*p && (*p < '0' || *p > '9'))
    p++;
  if(!*p)
    return -1;

  return atoi(p);
}

static int
read_root_free_bytes(uint *out)
{
  char buf[1024];
  char *line;
  char *p;

  if(out == 0)
    return -1;
  if(read_file_text("/proc/mountstats", buf, sizeof(buf)) < 0)
    return -1;

  p = buf;
  while(*p) {
    int fields = 0;
    char *tok[6];

    line = p;
    while(*p && *p != '\n') p++;
    if(*p == '\n') *p++ = 0;

    while(*line == ' ' || *line == '\t')
      line++;
    if(*line == 0)
      continue;

    tok[fields++] = line;
    while(*line && fields < 6) {
      if(*line == ' ' || *line == '\t') {
        *line = 0;
        line++;
        while(*line == ' ' || *line == '\t')
          line++;
        if(*line)
          tok[fields++] = line;
      } else {
        line++;
      }
    }

    if(fields < 6)
      continue;
    if(strcmp(tok[0], "dev") == 0 && strcmp(tok[1], "path") == 0)
      continue;
    if(strcmp(tok[1], "/") != 0)
      continue;

    {
      int free_blocks = atoi(tok[4]);
      int block_size = atoi(tok[5]);
      if(free_blocks < 0 || block_size <= 0)
        return -1;
      *out = (uint)free_blocks * (uint)block_size;
      return 0;
    }
  }

  return -1;
}

static void
human_size(uint bytes, char *out, int outsz)
{
  static const char suffix[] = "BKMG";
  uint div;
  uint v;
  int idx;

  if(out == 0 || outsz <= 2)
    return;

  div = 1;
  idx = 0;
  while(idx < 3 && bytes / div >= 1024) {
    div *= 1024;
    idx++;
  }

  v = bytes / div;
  snprintf(out, outsz, "%u%c", v, suffix[idx]);
}

static void
print_motd(void)
{
  static char motd[LOGIN_BUF_MAX];
  static char host[LOGIN_HOST_MAX];
  char mem[16];
  char disk[16];
  int mem_kb;
  uint disk_bytes;
  char *p;

  if(read_file_text(MOTD_PATH, motd, sizeof(motd)) < 0)
    return;

  if(read_file_text("/etc/hostname", host, sizeof(host)) < 0 || host[0] == 0)
    strcpy(host, "auxv6");
  trim(host);

  mem_kb = read_memfree_kb();
  if(mem_kb < 0)
    strcpy(mem, "n/a");
  else
    human_size((uint)mem_kb * 1024, mem, sizeof(mem));

  if(read_root_free_bytes(&disk_bytes) < 0)
    strcpy(disk, "n/a");
  else
    human_size(disk_bytes, disk, sizeof(disk));

  p = motd;
  while(*p) {
    if(strncmp(p, "@HOSTNAME@", 10) == 0) {
      write(1, host, strlen(host));
      p += 10;
      continue;
    }
    if(strncmp(p, "@FREE_MEM@", 10) == 0) {
      write(1, mem, strlen(mem));
      p += 10;
      continue;
    }
    if(strncmp(p, "@FREE_DISK@", 11) == 0) {
      write(1, disk, strlen(disk));
      p += 11;
      continue;
    }
    write(1, p, 1);
    p++;
  }
}

static void
make_login_shell_name(char *dst, int dstsz, const char *shell)
{
  const char *base;
  int i;

  if(dst == 0 || dstsz <= 1 || shell == 0)
    return;

  base = shell;
  for(i = 0; shell[i]; i++)
    if(shell[i] == '/')
      base = shell + i + 1;

  dst[0] = '-';
  snprintf(dst + 1, dstsz - 1, "%s", base);
}

int
main(int argc, char *argv[])
{
  char user[LOGIN_NAME_MAX];
  char pass[LOGIN_PASS_MAX];
  char login_argv0[LOGIN_NAME_MAX + 2];
  char *sh_argv[2];
  struct session_user su;
  int i;

  (void)argc;
  (void)argv;

  logdbg("main enter");
  ensure_stdio();
  logdbg("ensure_stdio done");
  umask(022);
  logdbg("umask done");

  for(i = 0; i < LOGIN_RETRIES; i++) {
    logdbg("loop top");
    claim_tty_foreground();
    logdbg("tty foreground claimed");
    restore_tty_cooked_mode();
    logdbg("tty cooked mode restored");
    dprintf(1, "login: ");
    logdbg("login prompt printed");
    memset(user, 0, sizeof(user));
    logdbg("before read_username");
    if(read_username(user, sizeof(user)) < 0) {
      logdbg("read_username returned < 0");
      exit(0);
    }
    logdbg("after read_username");

    trim(user);
    logdbg("after trim username");
    if(user[0] == 0) {
      logdbg("empty username");
      i--;
      continue;
    }

    logdbg("before password prompt");
    dprintf(1, "Password: ");
    logdbg("after password prompt");
    memset(pass, 0, sizeof(pass));
    logdbg("before readpass");
    if(readpass(pass, sizeof(pass)) == 0) {
      logdbg("readpass returned 0");
      exit(0);
    }
    logdbg("after readpass");
    trim(pass);
    logdbg("after trim password");

    logdbg("before lookup_user_local");
    {
      const char *auth_pass;

      if(lookup_user_local(user, &su) < 0) {
        logdbg("auth failed");
        dprintf(1, "Login incorrect\n");
        continue;
      }

      auth_pass = lookup_shadow_password(user);
      if(auth_pass == 0)
        auth_pass = su.pass;

      if(!verify_password(pass, auth_pass)) {
        logdbg("auth failed");
        dprintf(1, "Login incorrect\n");
        continue;
      }
    }
    logdbg("auth success");

    logdbg("before nologin check");
    show_nologin_and_exit_if_needed(&su);
    logdbg("after nologin check");

    logdbg("before setgroups/setgid/setuid");
    {
      gid_t groups[1];

      groups[0] = (gid_t)su.gid;
      if(setgroups(1, groups) < 0 || setgid(su.gid) < 0 || setuid(su.uid) < 0) {
        logdbg("setgroups/setgid/setuid failed");
        dprintf(1, "login: permission denied\n");
        continue;
      }
    }
    logdbg("after setgroups/setgid/setuid");

    set_session_env(&su);
    logdbg("after set_session_env");
    if(chdir(su.home) < 0)
      chdir("/");
    logdbg("after chdir");

    print_motd();
    logdbg("after print_motd");
    record_utmp_wtmp(&su);
    logdbg("after record_utmp_wtmp");

    if(su.shell[0] == 0)
      strcpy(su.shell, "/bin/sh");

    make_login_shell_name(login_argv0, sizeof(login_argv0), su.shell);
    sh_argv[0] = login_argv0;
    sh_argv[1] = 0;
    logdbg("before exec shell");
    exec(su.shell, sh_argv);

    logdbg("exec failed");
    dprintf(1, "login: exec %s failed\n", su.shell);
    exit(0);
  }

  dprintf(1, "login: too many attempts\n");
  exit(0);
}
