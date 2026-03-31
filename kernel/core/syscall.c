#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "syscall.h"

// User code makes a system call with INT T_SYSCALL.
// System call number in %eax.
// Arguments on the stack, from the user call to the C
// library system call function. The saved user %esp points
// to a saved program counter, and then the first argument.

// Fetch the int at addr from the current process.
int
fetchint(uint addr, int *ip)
{
  struct proc *curproc = myproc();

  if(addr >= curproc->sz || addr+4 > curproc->sz)
    return -1;
  *ip = *(int*)(addr);
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Doesn't actually copy the string - just sets *pp to point at it.
// Returns length of string, not including nul.
int
fetchstr(uint addr, char **pp)
{
  char *s, *ep;
  struct proc *curproc = myproc();

  if(addr >= curproc->sz)
    return -1;
  *pp = (char*)addr;
  ep = (char*)curproc->sz;
  for(s = *pp; s < ep; s++){
    if(*s == 0)
      return s - *pp;
  }
  return -1;
}

// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
  return fetchint((myproc()->tf->esp) + 4 + 4*n, ip);
}

// Fetch the nth word-sized system call argument as a pointer
// to a block of memory of size bytes.  Check that the pointer
// lies within the process address space.
int
argptr(int n, char **pp, int size)
{
  int i;
  struct proc *curproc = myproc();
 
  if(argint(n, &i) < 0)
    return -1;
  if(size < 0 || (uint)i >= curproc->sz || (uint)i+size > curproc->sz)
    return -1;
  *pp = (char*)i;
  return 0;
}

// Fetch the nth word-sized system call argument as a string pointer.
// Check that the pointer is valid and the string is nul-terminated.
// (There is no shared writable memory, so the string can't change
// between this check and being used by the kernel.)
int
argstr(int n, char **pp)
{
  int addr;
  if(argint(n, &addr) < 0)
    return -1;
  return fetchstr(addr, pp);
}

extern int sys_chdir(void);
extern int sys_close(void);
extern int sys_dup(void);
extern int sys_exec(void);
extern int sys_exit(void);
extern int sys_fork(void);
extern int sys_fstat(void);
extern int sys_getpid(void);
extern int sys_waitpid(void);
extern int sys_wait4(void);
extern int sys_waitid(void);
extern int sys_getppid(void);
extern int sys_getpgrp(void);
extern int sys_getuid(void);
extern int sys_getgid(void);
extern int sys_getcwd(void);
extern int sys_setpgid(void);
extern int sys_setsid(void);
extern int sys_setuid(void);
extern int sys_setgid(void);
extern int sys_chmod(void);
extern int sys_chown(void);
extern int sys_mountinfo(void);
extern int sys_mount(void);
extern int sys_umount(void);
extern int sys_uname(void);
extern int sys_stat(void);
extern int sys_kill(void);
extern int sys_sigsend(void);
extern int sys_sigaction(void);
extern int sys_sigprocmask(void);
extern int sys_sigreturn(void);
extern int sys_alarm(void);
extern int sys_tcsetpgrp(void);
extern int sys_tcgetpgrp(void);
extern int sys_tcgetattr(void);
extern int sys_tcsetattr(void);
extern int sys_link(void);
extern int sys_rename(void);
extern int sys_mkdir(void);
extern int sys_mknod(void);
extern int sys_open(void);
extern int sys_pipe(void);
extern int sys_read(void);
extern int sys_sbrk(void);
extern int sys_sleep(void);
extern int sys_unlink(void);
extern int sys_wait(void);
extern int sys_write(void);
extern int sys_uptime(void);
extern int sys_socket(void);
extern int sys_bind(void);
extern int sys_connect(void);
extern int sys_send(void);
extern int sys_recv(void);
extern int sys_listen(void);
extern int sys_accept(void);
extern int sys_recvtimeout(void);
extern int sys_netifinfo(void);
extern int sys_routeinfo(void);
extern int sys_arpinfo(void);
extern int sys_routeadd(void);
extern int sys_netifsetaddr(void);
extern int sys_devblocks(void);
extern int sys_getdents(void);
extern int sys_ext2fail(void);
extern int sys_fsfault(void);
extern int sys_lseek(void);
extern int sys_dup2(void);
extern int sys_fcntl(void);

static int (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_waitpid] sys_waitpid,
[SYS_wait4]   sys_wait4,
[SYS_waitid]  sys_waitid,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_getppid] sys_getppid,
[SYS_getpgrp] sys_getpgrp,
[SYS_getuid]  sys_getuid,
[SYS_getgid]  sys_getgid,
[SYS_getcwd]  sys_getcwd,
[SYS_setpgid] sys_setpgid,
[SYS_setsid]  sys_setsid,
[SYS_setuid]  sys_setuid,
[SYS_setgid]  sys_setgid,
[SYS_tcsetpgrp] sys_tcsetpgrp,
[SYS_tcgetpgrp] sys_tcgetpgrp,
[SYS_sigprocmask] sys_sigprocmask,
[SYS_tcgetattr] sys_tcgetattr,
[SYS_tcsetattr] sys_tcsetattr,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_chmod]   sys_chmod,
[SYS_chown]   sys_chown,
[SYS_mountinfo] sys_mountinfo,
[SYS_mount]   sys_mount,
[SYS_umount]  sys_umount,
[SYS_uname]   sys_uname,
  [SYS_stat]    sys_stat,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_rename]  sys_rename,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_socket]  sys_socket,
[SYS_bind]    sys_bind,
[SYS_connect] sys_connect,
[SYS_send]    sys_send,
[SYS_recv]    sys_recv,
[SYS_listen]  sys_listen,
[SYS_accept]  sys_accept,
[SYS_recvtimeout] sys_recvtimeout,
[SYS_netifinfo] sys_netifinfo,
[SYS_routeinfo] sys_routeinfo,
[SYS_arpinfo] sys_arpinfo,
[SYS_routeadd] sys_routeadd,
[SYS_netifsetaddr] sys_netifsetaddr,
[SYS_devblocks] sys_devblocks,
[SYS_getdents] sys_getdents,
[SYS_ext2fail] sys_ext2fail,
[SYS_fsfault] sys_fsfault,
[SYS_sigsend] sys_sigsend,
[SYS_sigaction] sys_sigaction,
[SYS_sigreturn] sys_sigreturn,
[SYS_alarm] sys_alarm,
[SYS_lseek] sys_lseek,
[SYS_dup2] sys_dup2,
[SYS_fcntl] sys_fcntl,
};

void
syscall(void)
{
  int num;
  struct proc *curproc = myproc();

  num = curproc->tf->eax;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    curproc->tf->eax = syscalls[num]();
  } else {
    cprintf("%d %s: unknown sys call %d\n",
            curproc->pid, curproc->name, num);
    curproc->tf->eax = -1;
  }
}
