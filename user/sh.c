// Shell.

#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "fs.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10
#define MAXJOBS 16
#define JOB_MAX_PROCS 16

#define JOB_RUNNING 1
#define JOB_STOPPED 2

#define PATH_MAX 128
#define PROMPT_MAX 128
#define USER_MAX 32
#define HOST_MAX 32
#define CWD_MAX 128
#define HISTORY_MAX 64
#define HISTORY_LINE_MAX 100
#define HISTORY_PATH_MAX (CWD_MAX + 16)

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);
void reapchildren(void);
int maybe_builtin(char *buf, int shell_pgid);
int strip_background(char *buf);
int add_job(int pgid, char *cmdline, int state);
void remove_job_by_index(int idx);
int find_job_by_pgid(int pgid);
int find_job_by_member_pid(int pid);
int find_job_by_jid(int jid);
int find_latest_job_index(void);
int parse_job_arg(char *arg, int *jid_out);
void mark_job_member_exit(int idx, int pid);
void jobs_critical_enter(sigset_t *oldmask);
void jobs_critical_leave(const sigset_t *oldmask);
void print_jobs(void);
void load_profile(void);
void load_hostname(void);
void load_passwd_defaults(void);
void sh_copy(char *dst, const char *src, int dstsz);
void sh_append(char *dst, const char *src, int dstsz);
void set_shell_var(const char *key, const char *value);
void print_shell_var(const char *key);
void print_shell_vars(void);
void prompt_string(char *out, int outsz);
void format_cwd_for_prompt(char *out, int outsz);
void trim_trailing_ws(char *s);
void update_cwd_after_cd(const char *path);
void sync_cwd_from_kernel(void);
void exec_with_path(char *cmd, char **argv);
void set_interactive_signal_handlers(void);
void reset_child_signal_handlers(void);
void history_init(void);
void history_add(const char *line);
int history_readline(char *buf, int nbuf, const char *prompt);

static char sh_path[PATH_MAX] = "/:/bin:/sbin";
static char sh_prompt[PROMPT_MAX] = "\\u:\\w";
static char sh_user[USER_MAX] = "root";
static char sh_host[HOST_MAX] = "auxv6";
static char sh_cwd[CWD_MAX] = "/";
static char sh_home[CWD_MAX] = "/";
static int sh_uid = 0;
static char sh_history[HISTORY_MAX][HISTORY_LINE_MAX];
static int sh_history_count = 0;
static char sh_history_path[HISTORY_PATH_MAX];

struct job {
  int used;
  int jid;
  int pgid;
  int leader_pid;
  int nprocs;
  int live_procs;
  int member_pids[JOB_MAX_PROCS];
  int state;
  char cmd[100];
};

static struct job jobs[MAXJOBS];
static int next_jid = 1;
// Execute cmd.  Never returns.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit(0);

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(0);
    exec_with_path(ecmd->argv[0], ecmd->argv);
    dprintf(2, "%s: command not found\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      dprintf(2, "cannot open %s for output (permission denied?)\n", rcmd->file);
      exit(0);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait();
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if(fork1() == 0){
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait();
    wait();
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}
#pragma GCC diagnostic pop

int
getcmd(char *buf, int nbuf)
{
  char prompt[256];

  prompt_string(prompt, sizeof(prompt));
  memset(buf, 0, nbuf);
  if(history_readline(buf, nbuf, prompt) < 0)
    return -1;
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd;
  int shell_pgid;
  int bg;
  int pid;
  int status;
  sigset_t oldmask;

  // Ensure that three file descriptors are open.
  while((fd = open("/dev/console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  load_hostname();
  load_passwd_defaults();
  load_profile();
  history_init();
  sync_cwd_from_kernel();
  set_interactive_signal_handlers();

  // Put the shell in its own process group and claim console foreground.
  setpgid(0, 0);
  shell_pgid = getpgrp();
  if(shell_pgid > 0)
    tcsetpgrp(shell_pgid);

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    char histline[sizeof(buf)];

    reapchildren();

    sh_copy(histline, buf, sizeof(histline));
    trim_trailing_ws(histline);
    if(histline[0] != 0)
      history_add(histline);

    if(maybe_builtin(buf, shell_pgid))
      continue;

    bg = strip_background(buf);
    if(buf[0] == 0)
      continue;

    if(buf[0] == 'c' && buf[1] == 'd' && (buf[2] == ' ' || buf[2] == '\n' || buf[2] == '\r' || buf[2] == 0)){
      // Chdir must be called by the parent, not the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      const char *path;
      if(buf[2] == ' '){
        trim_trailing_ws(buf + 3);
        path = buf + 3;
      } else {
        path = sh_home;
      }
      if(chdir(path) < 0)
        dprintf(2, "cannot cd %s\n", path);
      else
        update_cwd_after_cd(path);
      continue;
    }

    pid = fork1();
    if(pid == 0){
      reset_child_signal_handlers();
      setpgid(0, 0);
      runcmd(parsecmd(buf));
    }

    setpgid(pid, pid);

    if(bg) {
      jobs_critical_enter(&oldmask);
      int jid = add_job(pid, buf, JOB_RUNNING);
      jobs_critical_leave(&oldmask);
      if(jid > 0)
        dprintf(2, "[%d] %d\n", jid, pid);
      continue;
    }

    tcsetpgrp(pid);

    while(waitpid(-pid, &status, WUNTRACED) == pid){
      if(WIFSTOPPED(status)) {
        jobs_critical_enter(&oldmask);
        int jid = add_job(pid, buf, JOB_STOPPED);
        jobs_critical_leave(&oldmask);
        if(jid > 0)
          dprintf(2, "[%d] stopped\n", jid);
        break;
      }
      if(WIFEXITED(status) || WIFSIGNALED(status))
        break;
    }

    tcsetpgrp(shell_pgid);
  }
  exit(0);
}

static void
history_build_path(void)
{
  if(sh_home[0] == 0 || strcmp(sh_home, "/") == 0) {
    sh_copy(sh_history_path, "/.6sh_history", sizeof(sh_history_path));
    return;
  }

  sh_copy(sh_history_path, sh_home, sizeof(sh_history_path));
  if(strcmp(sh_history_path, "/") != 0)
    sh_append(sh_history_path, "/", sizeof(sh_history_path));
  sh_append(sh_history_path, ".6sh_history", sizeof(sh_history_path));
}

static void
history_push_in_memory(const char *line)
{
  int i;

  if(line == 0 || line[0] == 0)
    return;

  if(sh_history_count > 0 &&
     strcmp(sh_history[sh_history_count - 1], line) == 0)
    return;

  if(sh_history_count < HISTORY_MAX) {
    sh_copy(sh_history[sh_history_count], line, HISTORY_LINE_MAX);
    sh_history_count++;
    return;
  }

  for(i = 1; i < HISTORY_MAX; i++)
    sh_copy(sh_history[i - 1], sh_history[i], HISTORY_LINE_MAX);
  sh_copy(sh_history[HISTORY_MAX - 1], line, HISTORY_LINE_MAX);
}

static void
history_save(void)
{
  int fd;
  int i;

  fd = open(sh_history_path, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0)
    return;

  for(i = 0; i < sh_history_count; i++) {
    int n;

    n = strlen(sh_history[i]);
    if(n <= 0)
      continue;
    write(fd, sh_history[i], n);
    write(fd, "\n", 1);
  }

  close(fd);
}

void
history_init(void)
{
  int fd;
  int n;
  int i;
  int start;
  char buf[HISTORY_MAX * HISTORY_LINE_MAX + 2];
  char line[HISTORY_LINE_MAX];

  sh_history_count = 0;
  history_build_path();

  fd = open(sh_history_path, O_RDONLY);
  if(fd < 0)
    return;

  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0)
    return;

  buf[n] = 0;
  start = 0;
  for(i = 0; i <= n; i++) {
    if(buf[i] == '\n' || buf[i] == 0) {
      int len;

      len = i - start;
      if(len > 0) {
        if(len >= HISTORY_LINE_MAX)
          len = HISTORY_LINE_MAX - 1;
        memmove(line, buf + start, len);
        line[len] = 0;
        trim_trailing_ws(line);
        history_push_in_memory(line);
      }
      start = i + 1;
    }
  }
}

void
history_add(const char *line)
{
  history_push_in_memory(line);
  history_save();
}

static void
history_replace_line(char *line, int *len, const char *src, int max)
{
  int i;
  int oldlen;
  int newlen;

  oldlen = *len;
  for(i = 0; i < oldlen; i++)
    write(2, "\b \b", 3);

  newlen = strlen(src);
  if(newlen > max - 2)
    newlen = max - 2;
  if(newlen < 0)
    newlen = 0;

  if(newlen > 0)
    memmove(line, src, newlen);
  line[newlen] = 0;
  *len = newlen;

  if(newlen > 0)
    write(2, line, newlen);
}

int
history_readline(char *buf, int nbuf, const char *prompt)
{
  struct termios oldt;
  struct termios newt;
  int have_termios;
  int len;
  int nav;
  int cc;
  char c;
  char current[HISTORY_LINE_MAX];

  if(nbuf <= 1)
    return -1;

  dprintf(2, "%s", prompt);
  buf[0] = 0;
  current[0] = 0;
  len = 0;
  nav = -1;

  have_termios = (tcgetattr(0, &oldt) == 0);
  if(have_termios) {
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    if(tcsetattr(0, TCSANOW, &newt) < 0)
      have_termios = 0;
  }

  for(;;) {
    cc = read(0, &c, 1);
    if(cc < 1) {
      if(len == 0) {
        if(have_termios)
          tcsetattr(0, TCSANOW, &oldt);
        return -1;
      }
      break;
    }

    if(c == '\r' || c == '\n') {
      write(2, "\n", 1);
      break;
    }

    if(c == 4) {
      if(len == 0) {
        if(have_termios)
          tcsetattr(0, TCSANOW, &oldt);
        return -1;
      }
      continue;
    }

    if(c == '\b' || c == '\x7f') {
      if(len > 0) {
        len--;
        buf[len] = 0;
        write(2, "\b \b", 3);
      }
      continue;
    }

    if(c == '\033') {
      char c2;
      char c3;

      if(read(0, &c2, 1) < 1)
        continue;
      if(c2 != '[' && c2 != 'O')
        continue;
      if(read(0, &c3, 1) < 1)
        continue;

      if(c3 == 'A') {
        if(sh_history_count <= 0)
          continue;
        if(nav < 0)
          sh_copy(current, buf, sizeof(current));
        if(nav + 1 < sh_history_count)
          nav++;
        history_replace_line(buf, &len,
                             sh_history[sh_history_count - 1 - nav],
                             nbuf);
      } else if(c3 == 'B') {
        if(nav < 0)
          continue;
        nav--;
        if(nav >= 0)
          history_replace_line(buf, &len,
                               sh_history[sh_history_count - 1 - nav],
                               nbuf);
        else
          history_replace_line(buf, &len, current, nbuf);
      }
      continue;
    }

    if(c >= 0x20 && c < 0x7f) {
      if(len < nbuf - 2) {
        if(nav >= 0) {
          nav = -1;
          current[0] = 0;
        }
        buf[len++] = c;
        buf[len] = 0;
        write(2, &c, 1);
      }
    }
  }

  if(have_termios)
    tcsetattr(0, TCSANOW, &oldt);

  if(len >= nbuf - 1)
    len = nbuf - 2;
  buf[len++] = '\n';
  buf[len] = 0;
  return 0;
}

void
set_interactive_signal_handlers(void)
{
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_IGN;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, 0);
}

void
reset_child_signal_handlers(void)
{
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, 0);
}

void
load_passwd_defaults(void)
{
  int fd;
  int n;
  int i;
  int j;
  int line_start;
  int uidval;
  char buf[512];
  int uid;

  uid = getuid();
  if(uid >= 0)
    sh_uid = uid;

  fd = open("/etc/passwd", O_RDONLY);
  if(fd < 0)
    return;

  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0)
    return;
  buf[n] = 0;

  line_start = 0;
  for(i = 0; i <= n; i++) {
    int fstart[8];
    int flen[8];
    int nf;

    if(i < n && buf[i] != '\n' && buf[i] != '\r')
      continue;

    nf = 0;
    fstart[0] = line_start;
    for(j = line_start; j <= i; j++) {
      if(j == i || buf[j] == ':') {
        if(nf < 8) {
          flen[nf] = j - fstart[nf];
          nf++;
        }
        if(j < i && nf < 8)
          fstart[nf] = j + 1;
      }
    }

    line_start = i + 1;
    if(nf < 6)
      continue;

    uidval = 0;
    for(j = 0; j < flen[2]; j++) {
      char c;

      c = buf[fstart[2] + j];
      if(c < '0' || c > '9') {
        uidval = -1;
        break;
      }
      uidval = uidval * 10 + (c - '0');
    }
    if(uidval != sh_uid)
      continue;

    if(flen[0] > 0) {
      int ncopy;

      ncopy = flen[0];
      if(ncopy >= USER_MAX)
        ncopy = USER_MAX - 1;
      memmove(sh_user, buf + fstart[0], ncopy);
      sh_user[ncopy] = 0;
    }

    if(flen[5] > 0) {
      int ncopy;

      ncopy = flen[5];
      if(ncopy >= CWD_MAX)
        ncopy = CWD_MAX - 1;
      memmove(sh_home, buf + fstart[5], ncopy);
      sh_home[ncopy] = 0;
    } else
      sh_copy(sh_home, "/", sizeof(sh_home));
    return;
  }

  sh_copy(sh_home, "/", sizeof(sh_home));
}

void
load_hostname(void)
{
  int fd;
  int n;
  int i;
  char buf[HOST_MAX];

  fd = open("/etc/hostname", O_RDONLY);
  if(fd < 0)
    return;
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0)
    return;

  buf[n] = 0;
  for(i = 0; buf[i]; i++) {
    if(buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ' || buf[i] == '\t') {
      buf[i] = 0;
      break;
    }
  }

  if(buf[0])
    sh_copy(sh_host, buf, sizeof(sh_host));
}

void
set_shell_var(const char *key, const char *value)
{
  if(strncmp(key, "PATH", 4) == 0){
    sh_copy(sh_path, value, sizeof(sh_path));
    return;
  }
  if(strncmp(key, "PROMPT", 6) == 0){
    sh_copy(sh_prompt, value, sizeof(sh_prompt));
    return;
  }
  if(strncmp(key, "USER", 4) == 0){
    sh_copy(sh_user, value, sizeof(sh_user));
    return;
  }
  if(strncmp(key, "HOST", 4) == 0){
    sh_copy(sh_host, value, sizeof(sh_host));
    return;
  }
  if(strncmp(key, "HOME", 4) == 0){
    sh_copy(sh_home, value, sizeof(sh_home));
    return;
  }
}

void
sh_copy(char *dst, const char *src, int dstsz)
{
  int i;

  if(dstsz <= 0)
    return;
  for(i = 0; i < dstsz - 1 && src[i]; i++)
    dst[i] = src[i];
  dst[i] = 0;
}

void
sh_append(char *dst, const char *src, int dstsz)
{
  int dlen;
  int i;

  if(dstsz <= 0)
    return;
  dlen = strlen(dst);
  if(dlen >= dstsz - 1)
    return;
  for(i = 0; dlen + i < dstsz - 1 && src[i]; i++)
    dst[dlen + i] = src[i];
  dst[dlen + i] = 0;
}

void
load_profile(void)
{
  int fd;
  int n;
  char buf[256];
  char *line;

  fd = open("/etc/profile", O_RDONLY);
  if(fd < 0)
    return;

  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0)
    return;

  buf[n] = 0;
  line = buf;
  while(*line){
    char *next;
    char *value;

    while(*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n')
      line++;
    if(*line == 0)
      break;

    if(*line == '#'){
      while(*line && *line != '\n')
        line++;
      continue;
    }

    next = line;
    while(*next && *next != '\n' && *next != '\r')
      next++;

    value = strchr(line, '=');
    if(value != 0 && value < next){
      char key[16];
      char val[PATH_MAX];
      int klen;
      int vlen;

      klen = value - line;
      if(klen > 0 && klen < (int)sizeof(key)){
        memmove(key, line, klen);
        key[klen] = 0;

        value++;
        vlen = next - value;
        if(vlen >= (int)sizeof(val))
          vlen = sizeof(val) - 1;
        if(vlen > 0){
          memmove(val, value, vlen);
          val[vlen] = 0;
          set_shell_var(key, val);
        }
      }
    }

    line = next;
  }
}

void
prompt_string(char *out, int outsz)
{
  int i;
  int j;
  int uid_now;
  int marker;
  int has_marker_token;
  char cwd_buf[CWD_MAX];

  if(outsz <= 0)
    return;

  uid_now = getuid();
  if(uid_now >= 0)
    sh_uid = uid_now;
  sync_cwd_from_kernel();

  marker = (sh_uid == 0) ? '#' : '$';
  has_marker_token = 0;
  format_cwd_for_prompt(cwd_buf, sizeof(cwd_buf));
  j = 0;

  for(i = 0; sh_prompt[i] && j < outsz - 3; i++){
    if(sh_prompt[i] == '\\'){
      i++;
      if(!sh_prompt[i])
        break;
      if(sh_prompt[i] == 'u'){
        int k;
        for(k = 0; sh_user[k] && j < outsz - 3; k++)
          out[j++] = sh_user[k];
      } else if(sh_prompt[i] == 'h'){
        int k;
        for(k = 0; sh_host[k] && j < outsz - 3; k++)
          out[j++] = sh_host[k];
      } else if(sh_prompt[i] == 'w'){
        int k;
        for(k = 0; cwd_buf[k] && j < outsz - 3; k++)
          out[j++] = cwd_buf[k];
      } else if(sh_prompt[i] == '$'){
        out[j++] = marker;
        has_marker_token = 1;
      } else if(sh_prompt[i] == '\\'){
        out[j++] = '\\';
      } else {
        out[j++] = sh_prompt[i];
      }
      continue;
    }
    out[j++] = sh_prompt[i];
  }

  if(!has_marker_token)
    out[j++] = marker;
  out[j++] = ' ';
  out[j] = 0;
}

void
format_cwd_for_prompt(char *out, int outsz)
{
  int hlen;

  if(outsz <= 0)
    return;

  if(sh_home[0] == 0) {
    sh_copy(out, sh_cwd, outsz);
    return;
  }

  if(strcmp(sh_home, "/") == 0) {
    if(strcmp(sh_cwd, "/") == 0) {
      sh_copy(out, "~", outsz);
      return;
    }
    if(sh_cwd[0] == '/') {
      sh_copy(out, "~", outsz);
      sh_append(out, sh_cwd, outsz);
      return;
    }
    sh_copy(out, sh_cwd, outsz);
    return;
  }

  hlen = strlen(sh_home);
  if(hlen > 0 && strncmp(sh_cwd, sh_home, hlen) == 0 &&
     (sh_cwd[hlen] == 0 || sh_cwd[hlen] == '/')) {
    sh_copy(out, "~", outsz);
    if(sh_cwd[hlen] == '/')
      sh_append(out, sh_cwd + hlen, outsz);
    return;
  }

  sh_copy(out, sh_cwd, outsz);
}

void
trim_trailing_ws(char *s)
{
  int n;

  n = strlen(s);
  while(n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\n' || s[n-1] == '\r'))
    n--;
  s[n] = 0;
}

void
update_cwd_after_cd(const char *path)
{
  char newcwd[CWD_MAX];
  char segment[DIRSIZ + 1];
  int i;
  int j;
  int start;
  int seglen;

  if(path == 0 || path[0] == 0)
    return;

  if(path[0] == '/')
    sh_copy(newcwd, "/", sizeof(newcwd));
  else
    sh_copy(newcwd, sh_cwd, sizeof(newcwd));

  i = 0;
  while(path[i]){
    while(path[i] == '/')
      i++;
    if(path[i] == 0)
      break;

    start = i;
    while(path[i] && path[i] != '/')
      i++;
    seglen = i - start;
    if(seglen <= 0)
      continue;

    if(seglen >= (int)sizeof(segment))
      seglen = sizeof(segment) - 1;
    memmove(segment, path + start, seglen);
    segment[seglen] = 0;

    if(strcmp(segment, ".") == 0)
      continue;
    if(strcmp(segment, "..") == 0){
      j = strlen(newcwd);
      while(j > 1 && newcwd[j-1] == '/')
        j--;
      while(j > 1 && newcwd[j-1] != '/')
        j--;
      if(j <= 1)
        j = 1;
      newcwd[j] = 0;
      continue;
    }

    j = strlen(newcwd);
    if(j > 1 && newcwd[j-1] == '/')
      newcwd[j-1] = 0;
    if(strcmp(newcwd, "/") != 0)
      sh_append(newcwd, "/", sizeof(newcwd));
    sh_append(newcwd, segment, sizeof(newcwd));
  }

  if(newcwd[0] == 0)
    sh_copy(newcwd, "/", sizeof(newcwd));

  sh_copy(sh_cwd, newcwd, sizeof(sh_cwd));
}

void
sync_cwd_from_kernel(void)
{
  char buf[CWD_MAX];

  if(getcwd(buf, sizeof(buf)) != 0 && buf[0] != 0)
    sh_copy(sh_cwd, buf, sizeof(sh_cwd));
}

void
print_shell_var(const char *key)
{
  int uid;

  uid = getuid();
  if(uid >= 0)
    sh_uid = uid;

  if(strcmp(key, "PATH") == 0)
    dprintf(2, "PATH=%s\n", sh_path);
  else if(strcmp(key, "PROMPT") == 0)
    dprintf(2, "PROMPT=%s\n", sh_prompt);
  else if(strcmp(key, "USER") == 0)
    dprintf(2, "USER=%s\n", sh_user);
  else if(strcmp(key, "HOST") == 0)
    dprintf(2, "HOST=%s\n", sh_host);
  else if(strcmp(key, "HOME") == 0)
    dprintf(2, "HOME=%s\n", sh_home);
  else if(strcmp(key, "UID") == 0)
    dprintf(2, "UID=%d\n", sh_uid);
  else if(strcmp(key, "PWD") == 0)
    dprintf(2, "PWD=%s\n", sh_cwd);
  else
    dprintf(2, "set: unknown variable %s\n", key);
}

void
print_shell_vars(void)
{
  int uid;

  uid = getuid();
  if(uid >= 0)
    sh_uid = uid;

  dprintf(2, "PATH=%s\n", sh_path);
  dprintf(2, "PROMPT=%s\n", sh_prompt);
  dprintf(2, "USER=%s\n", sh_user);
  dprintf(2, "HOST=%s\n", sh_host);
  dprintf(2, "HOME=%s\n", sh_home);
  dprintf(2, "UID=%d\n", sh_uid);
  dprintf(2, "PWD=%s\n", sh_cwd);
}

void
exec_with_path(char *cmd, char **argv)
{
  char candidate[PATH_MAX];
  char *seg;
  int cmdlen;

  if(strchr(cmd, '/')){
    exec(cmd, argv);
    return;
  }

  cmdlen = strlen(cmd);
  seg = sh_path;

  while(*seg){
    char *end;
    int dlen;
    int off;

    while(*seg == ':')
      seg++;
    if(*seg == 0)
      break;

    end = seg;
    while(*end && *end != ':')
      end++;
    dlen = end - seg;
    if(dlen <= 0){
      seg = end;
      continue;
    }

    off = dlen;
    if(off >= PATH_MAX - 1){
      seg = end;
      continue;
    }
    memmove(candidate, seg, off);
    if(candidate[off - 1] != '/'){
      candidate[off++] = '/';
    }

    if(off + cmdlen >= PATH_MAX){
      seg = end;
      continue;
    }
    memmove(candidate + off, cmd, cmdlen);
    candidate[off + cmdlen] = 0;

    exec(candidate, argv);
    seg = end;
  }

  // Relative fallback preserves current behavior for local binaries/scripts.
  exec(cmd, argv);
}

void
panic(char *s)
{
  dprintf(2, "%s\n", s);
  exit(0);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

void
reapchildren(void)
{
  int st;
  int pid;
  int idx;
  sigset_t oldmask;

  // Reap background jobs and observe state transitions.
  jobs_critical_enter(&oldmask);
  while((pid = waitpid(-1, &st, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
    idx = find_job_by_member_pid(pid);
    if(idx < 0)
      continue;
    if(WIFSTOPPED(st)) {
      jobs[idx].state = JOB_STOPPED;
      continue;
    }
    if(WIFCONTINUED(st)) {
      jobs[idx].state = JOB_RUNNING;
      continue;
    }
    if(WIFEXITED(st) || WIFSIGNALED(st))
      mark_job_member_exit(idx, pid);
  }
  jobs_critical_leave(&oldmask);
}

int
find_job_by_pgid(int pgid)
{
  int i;

  for(i = 0; i < MAXJOBS; i++)
    if(jobs[i].used && jobs[i].pgid == pgid)
      return i;
  return -1;
}

int
find_job_by_member_pid(int pid)
{
  int i;
  int j;

  for(i = 0; i < MAXJOBS; i++) {
    if(!jobs[i].used)
      continue;
    for(j = 0; j < jobs[i].nprocs && j < JOB_MAX_PROCS; j++)
      if(jobs[i].member_pids[j] == pid)
        return i;
  }
  return -1;
}

int
find_job_by_jid(int jid)
{
  int i;

  for(i = 0; i < MAXJOBS; i++)
    if(jobs[i].used && jobs[i].jid == jid)
      return i;
  return -1;
}

void
remove_job_by_index(int idx)
{
  if(idx < 0 || idx >= MAXJOBS)
    return;
  jobs[idx].used = 0;
  jobs[idx].jid = 0;
  jobs[idx].pgid = 0;
  jobs[idx].leader_pid = 0;
  jobs[idx].nprocs = 0;
  jobs[idx].live_procs = 0;
  memset(jobs[idx].member_pids, 0, sizeof(jobs[idx].member_pids));
  jobs[idx].state = 0;
  jobs[idx].cmd[0] = 0;
}

int
add_job(int pgid, char *cmdline, int state)
{
  int i;
  int j;
  int n;

  i = find_job_by_pgid(pgid);
  if(i < 0) {
    for(i = 0; i < MAXJOBS; i++)
      if(!jobs[i].used)
        break;
    if(i >= MAXJOBS)
      return -1;
    jobs[i].used = 1;
    jobs[i].jid = next_jid++;
    jobs[i].pgid = pgid;
    jobs[i].leader_pid = pgid;
    jobs[i].nprocs = 1;
    jobs[i].live_procs = 1;
    memset(jobs[i].member_pids, 0, sizeof(jobs[i].member_pids));
    jobs[i].member_pids[0] = pgid;
  }

  jobs[i].state = state;

  n = strlen(cmdline);
  if(n > 0 && cmdline[n-1] == '\n')
    n--;
  if(n >= sizeof(jobs[i].cmd))
    n = sizeof(jobs[i].cmd) - 1;

  for(j = 0; j < n; j++)
    jobs[i].cmd[j] = cmdline[j];
  jobs[i].cmd[n] = 0;

  return jobs[i].jid;
}

void
mark_job_member_exit(int idx, int pid)
{
  int i;

  if(idx < 0 || idx >= MAXJOBS || !jobs[idx].used)
    return;

  for(i = 0; i < jobs[idx].nprocs && i < JOB_MAX_PROCS; i++) {
    if(jobs[idx].member_pids[i] != pid)
      continue;
    jobs[idx].member_pids[i] = 0;
    if(jobs[idx].live_procs > 0)
      jobs[idx].live_procs--;
    break;
  }

  if(jobs[idx].live_procs <= 0)
    remove_job_by_index(idx);
}

void
jobs_critical_enter(sigset_t *oldmask)
{
  sigset_t mask;

  if(oldmask == 0)
    return;
  mask = SIGBIT(SIGCHLD);
  if(sigprocmask(SIG_BLOCK, &mask, oldmask) < 0)
    *oldmask = 0;
}

void
jobs_critical_leave(const sigset_t *oldmask)
{
  if(oldmask == 0)
    return;
  sigprocmask(SIG_SETMASK, oldmask, 0);
}

void
print_jobs(void)
{
  int i;
  int latest;
  char marker;
  char *state;

  latest = find_latest_job_index();

  for(i = 0; i < MAXJOBS; i++) {
    if(!jobs[i].used)
      continue;
    marker = (i == latest) ? '+' : ' ';
    state = (jobs[i].state == JOB_STOPPED) ? "stopped" : "running";
    dprintf(2, "[%d]%c %s %d %s\n", jobs[i].jid, marker, state, jobs[i].pgid, jobs[i].cmd);
  }
}

int
find_latest_job_index(void)
{
  int i;
  int best;

  best = -1;
  for(i = 0; i < MAXJOBS; i++) {
    if(!jobs[i].used)
      continue;
    if(best < 0 || jobs[i].jid > jobs[best].jid)
      best = i;
  }
  return best;
}

int
parse_job_arg(char *arg, int *jid_out)
{
  char *p;

  if(arg == 0 || jid_out == 0)
    return -1;

  p = arg;
  if(*p == '%')
    p++;
  if(*p == 0 || *p == '\n')
    return -1;

  while(*p && *p != '\n') {
    if(*p < '0' || *p > '9')
      return -1;
    p++;
  }

  *jid_out = atoi(arg[0] == '%' ? arg + 1 : arg);
  if(*jid_out <= 0)
    return -1;
  return 0;
}

int
strip_background(char *buf)
{
  int i;

  i = strlen(buf);
  while(i > 0 && (buf[i-1] == ' ' || buf[i-1] == '\t' || buf[i-1] == '\n'))
    i--;

  if(i > 0 && buf[i-1] == '&') {
    i--;
    while(i > 0 && (buf[i-1] == ' ' || buf[i-1] == '\t'))
      i--;
    buf[i] = 0;
    return 1;
  }

  return 0;
}

int
maybe_builtin(char *buf, int shell_pgid)
{
  char *p;
  char *arg;
  int jid;
  int idx;
  int status;
  int wpid;
  int pgid;
  sigset_t oldmask;

  p = buf;
  while(*p == ' ' || *p == '\t')
    p++;

  if(p[0] == 0 || p[0] == '\n')
    return 1;

  if(strncmp(p, "jobs", 4) == 0 && (p[4] == 0 || p[4] == '\n' || p[4] == ' ' || p[4] == '\t')) {
    print_jobs();
    return 1;
  }

  if(strncmp(p, "set", 3) == 0 && (p[3] == 0 || p[3] == '\n' || p[3] == ' ' || p[3] == '\t')) {
    char *arg;
    char *eq;
    char key[16];
    int klen;

    arg = p + 3;
    while(*arg == ' ' || *arg == '\t')
      arg++;
    if(*arg == 0 || *arg == '\n') {
      print_shell_vars();
      return 1;
    }

    trim_trailing_ws(arg);
    eq = strchr(arg, '=');
    if(eq == 0) {
      print_shell_var(arg);
      return 1;
    }

    klen = eq - arg;
    if(klen <= 0 || klen >= (int)sizeof(key)) {
      dprintf(2, "set: invalid variable name\n");
      return 1;
    }
    memmove(key, arg, klen);
    key[klen] = 0;
    set_shell_var(key, eq + 1);
    return 1;
  }

  if(strncmp(p, "fg", 2) == 0 && (p[2] == 0 || p[2] == '\n' || p[2] == ' ' || p[2] == '\t')) {
    jobs_critical_enter(&oldmask);
    arg = p + 2;
    while(*arg == ' ' || *arg == '\t')
      arg++;

    if(*arg == 0 || *arg == '\n') {
      idx = find_latest_job_index();
      if(idx < 0) {
        jobs_critical_leave(&oldmask);
        dprintf(2, "fg: no current job\n");
        return 1;
      }
    } else {
      if(parse_job_arg(arg, &jid) < 0) {
        jobs_critical_leave(&oldmask);
        dprintf(2, "usage: fg [%%jobid]\n");
        return 1;
      }

      idx = find_job_by_jid(jid);
      if(idx < 0) {
        jobs_critical_leave(&oldmask);
        dprintf(2, "fg: no such job %d\n", jid);
        return 1;
      }
    }

    pgid = jobs[idx].pgid;
    jobs[idx].state = JOB_RUNNING;
    jobs_critical_leave(&oldmask);

    tcsetpgrp(pgid);
    sigsend(-pgid, SIGCONT);

    while((wpid = waitpid(-pgid, &status, WUNTRACED)) > 0) {
      jobs_critical_enter(&oldmask);
      idx = find_job_by_pgid(pgid);
      if(idx < 0) {
        jobs_critical_leave(&oldmask);
        break;
      }
      if(WIFSTOPPED(status)) {
        jobs[idx].state = JOB_STOPPED;
        jobs_critical_leave(&oldmask);
        break;
      }
      if(WIFEXITED(status) || WIFSIGNALED(status)) {
        mark_job_member_exit(idx, wpid);
        if(find_job_by_pgid(pgid) < 0) {
          jobs_critical_leave(&oldmask);
          break;
        }
      }
      jobs_critical_leave(&oldmask);
    }

    tcsetpgrp(shell_pgid);
    return 1;
  }

  if(strncmp(p, "bg", 2) == 0 && (p[2] == 0 || p[2] == '\n' || p[2] == ' ' || p[2] == '\t')) {
    jobs_critical_enter(&oldmask);
    arg = p + 2;
    while(*arg == ' ' || *arg == '\t')
      arg++;

    if(*arg == 0 || *arg == '\n') {
      idx = find_latest_job_index();
      if(idx < 0) {
        jobs_critical_leave(&oldmask);
        dprintf(2, "bg: no current job\n");
        return 1;
      }
    } else {
      if(parse_job_arg(arg, &jid) < 0) {
        jobs_critical_leave(&oldmask);
        dprintf(2, "usage: bg [%%jobid]\n");
        return 1;
      }

      idx = find_job_by_jid(jid);
      if(idx < 0) {
        jobs_critical_leave(&oldmask);
        dprintf(2, "bg: no such job %d\n", jid);
        return 1;
      }
    }

    pgid = jobs[idx].pgid;
    jobs[idx].state = JOB_RUNNING;
    jobs_critical_leave(&oldmask);

    sigsend(-pgid, SIGCONT);
    return 1;
  }

  return 0;
}

//PAGEBREAK!
// Constructors

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    dprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_TRUNC, 1);
      break;
    case '+':  // >>
        cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_APPEND, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
