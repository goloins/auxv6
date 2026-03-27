// Shell.

#include "../include/types.h"
#include "../include/stat.h"
#include "../include/user.h"
#include "../include/fcntl.h"

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
    exit();

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit();
    exec(ecmd->argv[0], ecmd->argv);
    printf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      printf(2, "open %s failed\n", rcmd->file);
      exit();
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
  exit();
}
#pragma GCC diagnostic pop

int
getcmd(char *buf, int nbuf)
{
  printf(2, "$ ");
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // EOF
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
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  // Put the shell in its own process group and claim console foreground.
  setpgid(0, 0);
  shell_pgid = getpgrp();
  if(shell_pgid > 0)
    tcsetpgrp(shell_pgid);

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    reapchildren();

    if(maybe_builtin(buf, shell_pgid))
      continue;

    bg = strip_background(buf);
    if(buf[0] == 0)
      continue;

    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Chdir must be called by the parent, not the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
        printf(2, "cannot cd %s\n", buf+3);
      continue;
    }

    pid = fork1();
    if(pid == 0){
      setpgid(0, 0);
      runcmd(parsecmd(buf));
    }

    setpgid(pid, pid);

    if(bg) {
      jobs_critical_enter(&oldmask);
      int jid = add_job(pid, buf, JOB_RUNNING);
      jobs_critical_leave(&oldmask);
      if(jid > 0)
        printf(2, "[%d] %d\n", jid, pid);
      continue;
    }

    tcsetpgrp(pid);

    while(waitpid(-pid, &status, WUNTRACED) == pid){
      if(WIFSTOPPED(status)) {
        jobs_critical_enter(&oldmask);
        int jid = add_job(pid, buf, JOB_STOPPED);
        jobs_critical_leave(&oldmask);
        if(jid > 0)
          printf(2, "[%d] stopped\n", jid);
        break;
      }
      if(WIFEXITED(status) || WIFSIGNALED(status))
        break;
    }

    tcsetpgrp(shell_pgid);
  }
  exit();
}

void
panic(char *s)
{
  printf(2, "%s\n", s);
  exit();
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
    printf(2, "[%d]%c %s %d %s\n", jobs[i].jid, marker, state, jobs[i].pgid, jobs[i].cmd);
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

  if(strncmp(p, "fg", 2) == 0 && (p[2] == 0 || p[2] == '\n' || p[2] == ' ' || p[2] == '\t')) {
    jobs_critical_enter(&oldmask);
    arg = p + 2;
    while(*arg == ' ' || *arg == '\t')
      arg++;

    if(*arg == 0 || *arg == '\n') {
      idx = find_latest_job_index();
      if(idx < 0) {
        jobs_critical_leave(&oldmask);
        printf(2, "fg: no current job\n");
        return 1;
      }
    } else {
      if(parse_job_arg(arg, &jid) < 0) {
        jobs_critical_leave(&oldmask);
        printf(2, "usage: fg [%%jobid]\n");
        return 1;
      }

      idx = find_job_by_jid(jid);
      if(idx < 0) {
        jobs_critical_leave(&oldmask);
        printf(2, "fg: no such job %d\n", jid);
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
        printf(2, "bg: no current job\n");
        return 1;
      }
    } else {
      if(parse_job_arg(arg, &jid) < 0) {
        jobs_critical_leave(&oldmask);
        printf(2, "usage: bg [%%jobid]\n");
        return 1;
      }

      idx = find_job_by_jid(jid);
      if(idx < 0) {
        jobs_critical_leave(&oldmask);
        printf(2, "bg: no such job %d\n", jid);
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
    printf(2, "leftovers: %s\n", s);
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
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
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
