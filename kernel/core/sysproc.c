#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "termios.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_waitpid(void)
{
  int pid;
  int options;
  int staddr;
  int *status;

  if(argint(0, &pid) < 0 || argint(1, &staddr) < 0 || argint(2, &options) < 0)
    return -1;

  status = 0;
  if(staddr != 0) {
    if(argptr(1, (char**)&status, sizeof(int)) < 0)
      return -1;
  }

  return proc_waitpid(pid, status, options);
}

int
sys_wait4(void)
{
  int pid;
  int options;
  int staddr;
  int ruaddr;
  int *status;

  if(argint(0, &pid) < 0 || argint(1, &staddr) < 0 ||
     argint(2, &options) < 0 || argint(3, &ruaddr) < 0)
    return -1;

  status = 0;
  if(staddr != 0) {
    if(argptr(1, (char**)&status, sizeof(int)) < 0)
      return -1;
  }

  return proc_wait4(pid, status, options, (uint)ruaddr);
}

int
sys_waitid(void)
{
  int idtype;
  int id;
  int infoaddr;
  int options;
  int *infop;

  if(argint(0, &idtype) < 0 || argint(1, &id) < 0 ||
     argint(2, &infoaddr) < 0 || argint(3, &options) < 0)
    return -1;

  infop = 0;
  if(infoaddr != 0) {
    if(argptr(2, (char**)&infop, sizeof(int) * 4) < 0)
      return -1;
  }

  return proc_waitid(idtype, id, infop, options);
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_sigsend(void)
{
  int pid;
  int signo;

  if(argint(0, &pid) < 0 || argint(1, &signo) < 0)
    return -1;
  return proc_kill_with_signal(pid, signo);
}

int
sys_sigaction(void)
{
  int signo;
  int actaddr;
  int oldactaddr;
  uint *act;
  uint *oldact;

  if(argint(0, &signo) < 0 || argint(1, &actaddr) < 0 || argint(2, &oldactaddr) < 0)
    return -1;

  act = 0;
  if(actaddr != 0) {
    if(argptr(1, (char**)&act, sizeof(uint) * 3) < 0)
      return -1;
  }

  oldact = 0;
  if(oldactaddr != 0) {
    if(argptr(2, (char**)&oldact, sizeof(uint) * 3) < 0)
      return -1;
  }

  return proc_sigaction(signo, (uint)act, (uint)oldact);
}

int
sys_sigprocmask(void)
{
  int how;
  int setaddr;
  int oldsetaddr;
  sigset_t *set;
  sigset_t *oldset;

  if(argint(0, &how) < 0 || argint(1, &setaddr) < 0 || argint(2, &oldsetaddr) < 0)
    return -1;

  set = 0;
  if(setaddr != 0) {
    if(argptr(1, (char**)&set, sizeof(sigset_t)) < 0)
      return -1;
  }

  oldset = 0;
  if(oldsetaddr != 0) {
    if(argptr(2, (char**)&oldset, sizeof(sigset_t)) < 0)
      return -1;
  }

  return proc_sigprocmask(how, (uint)set, (uint)oldset);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_getppid(void)
{
  return proc_getppid();
}

int
sys_getpgrp(void)
{
  return proc_getpgrp();
}

int
sys_getuid(void)
{
  return proc_getuid();
}

int
sys_getgid(void)
{
  return proc_getgid();
}

int
sys_setpgid(void)
{
  int pid;
  int pgid;

  if(argint(0, &pid) < 0 || argint(1, &pgid) < 0)
    return -1;
  return proc_setpgid(pid, pgid);
}

int
sys_setsid(void)
{
  return proc_setsid();
}

int
sys_setuid(void)
{
  int uid;

  if(argint(0, &uid) < 0)
    return -1;
  return proc_setuid(uid);
}

int
sys_setgid(void)
{
  int gid;

  if(argint(0, &gid) < 0)
    return -1;
  return proc_setgid(gid);
}

int
sys_tcsetpgrp(void)
{
  int pgid;

  if(argint(0, &pgid) < 0)
    return -1;
  return proc_tcsetpgrp(pgid);
}

int
sys_tcgetpgrp(void)
{
  return proc_tcgetpgrp();
}

int
sys_tcgetattr(void)
{
  int fd;
  int termios_addr;
  struct termios *tp;

  if(argint(0, &fd) < 0 || argint(1, &termios_addr) < 0)
    return -1;

  tp = 0;
  if(termios_addr != 0) {
    if(argptr(1, (char**)&tp, sizeof(struct termios)) < 0)
      return -1;
  }

  return proc_tcgetattr(fd, (uint)tp);
}

int
sys_tcsetattr(void)
{
  int fd;
  int optional_actions;
  int termios_addr;
  struct termios *tp;

  if(argint(0, &fd) < 0 || argint(1, &optional_actions) < 0 ||
     argint(2, &termios_addr) < 0)
    return -1;

  tp = 0;
  if(termios_addr != 0) {
    if(argptr(2, (char**)&tp, sizeof(struct termios)) < 0)
      return -1;
  }

  return proc_tcsetattr(fd, optional_actions, (uint)tp);
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_uname(void)
{
  char *buf;
  int size;

  if(argint(1, &size) < 0)
    return -1;
  if(size <= 0)
    return -1;
  if(argptr(0, &buf, size) < 0)
    return -1;

  safestrcpy(buf, "a/ux86 aux86 i686", size);
  return 0;
}
