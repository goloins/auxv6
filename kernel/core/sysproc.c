#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "termios.h"
#include "signal.h"
#include "spinlock.h"

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

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

// Restore context from signal frame on user stack.
// Called when returning from a signal handler via the trampoline.
int
sys_sigreturn(void)
{
  struct proc *p;
  struct trapframe *tf;
  struct sigframe sf;
  uint sp;

  p = myproc();
  if(p == 0)
    return -1;

  tf = p->tf;
  if(tf == 0)
    return -1;

  // Signal frame is at current stack pointer (we're called via trampoline)
  // The trampoline was at sf_sigreturn, which is at the start of sigframe
  // so esp should point somewhere near the sigframe
  sp = tf->esp - sizeof(uint);  // Account for return address pushed by int

  // Look for the sigframe - esp should be at the frame's return addr location
  // after the int instruction. We need to back up to find the frame.
  // Actually, after int $T_SYSCALL in trampoline, esp points to right after
  // the int instruction's effect. The sigframe starts at the sf_sigreturn field.
  
  // The stack looks like:
  //   [sigframe]    <- we need to find this
  //   
  // When handler returned, it popped its frame and used "ret" to jump to
  // sf_sigreturn (the trampoline). After int $0x40, we're here.
  // esp now points to where it was when the int happened.
  // Since sigreturn has no args, the frame is at the base - we need to
  // reconstruct where the sigframe was.
  
  // The handler was called with esp pointing at sigframe (sf_sigreturn is first).
  // After handler does "ret", it pops sf_sigreturn and jumps there.
  // At that point esp = &sf.sf_signo (second field).
  // But the trampoline doesn't use stack, it just does int.
  // After int, esp is still at &sf.sf_signo... but that's user esp, saved in tf->esp.
  
  // Let me reconsider: when we set up the frame, we set:
  //   tf->esp = sp (top of sigframe)
  //   tf->eip = handler
  // Handler entry: esp points at sigframe (sf_sigreturn is "return addr")
  // Handler runs its code, then does "ret"
  // "ret" pops sf_sigreturn into eip, esp now = sp + 4 = &sf.sf_signo
  // Trampoline runs: mov $64,%eax; int $0x40
  // int saves user esp (which is sp+4) into tf->esp
  
  // So the sigframe starts at tf->esp - 4
  sp = tf->esp - sizeof(uint);

  // Copy sigframe from user stack
  if(copyin(p->pgdir, &sf, sp, sizeof(sf)) < 0) {
    cprintf("pid %d: sigreturn copyin failed\n", p->pid);
    return -1;
  }

  // Restore saved registers to trap frame
  tf->edi = sf.sf_edi;
  tf->esi = sf.sf_esi;
  tf->ebp = sf.sf_ebp;
  tf->ebx = sf.sf_ebx;
  tf->edx = sf.sf_edx;
  tf->ecx = sf.sf_ecx;
  tf->eax = sf.sf_eax;
  tf->eip = sf.sf_eip;
  tf->eflags = sf.sf_eflags;
  tf->esp = sf.sf_esp;

  // Restore signal mask
  acquire(&ptable.lock);
  p->sig_mask = sf.sf_oldmask;
  p->sig_mask &= ~(SIGBIT(SIGKILL) | SIGBIT(SIGSTOP));
  release(&ptable.lock);

  // Return value will be ignored since we restored eax
  return sf.sf_eax;
}

// Set an alarm to deliver SIGALRM after 'seconds' seconds.
// Returns the number of seconds remaining on a previous alarm, or 0.
int
sys_alarm(void)
{
  int seconds;
  struct proc *p;
  uint old_alarm;
  uint remaining;

  if(argint(0, &seconds) < 0)
    return -1;
  
  if(seconds < 0)
    return -1;

  p = myproc();
  old_alarm = p->alarm_ticks;
  
  // Calculate remaining time on old alarm
  if(old_alarm == 0) {
    remaining = 0;
  } else if(ticks >= old_alarm) {
    remaining = 0;  // Already expired
  } else {
    remaining = (old_alarm - ticks + 99) / 100;  // Round up to seconds
  }
  
  // Set new alarm (100 ticks per second)
  if(seconds == 0) {
    p->alarm_ticks = 0;  // Cancel alarm
  } else {
    p->alarm_ticks = ticks + (uint)seconds * 100;
  }
  
  return remaining;
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
