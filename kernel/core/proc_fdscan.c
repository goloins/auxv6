#include "types.h"
#include "defs.h"
#include "param.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "stat.h"
#include "fs.h"
#include "file.h"

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

// Phase 1A: Check if any process has an open file on the given device.
// Called by fs.c when unmounting or checking device in-use status.
int
file_has_refs_on_dev(uint dev)
{
  int i;
  struct proc *p;
  struct file *f;

  acquire(&ptable.lock);

  // Walk all processes' fdtables to find open files on device
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED || p->fdtable == 0)
      continue;

    for(i = 0; i < p->fdtable->nfds; i++){
      f = p->fdtable->entries[i];
      if(f == 0)
        continue;
      if(f->ref < 1)
        continue;
      if(f->type != FD_INODE || f->ip == 0)
        continue;
      if(f->ip->dev == dev){
        release(&ptable.lock);
        return 1;
      }
    }
  }

  release(&ptable.lock);
  return 0;
}

// Return 1 if any live process holds an open fd for the given PTY side/index.
// Used by PTY driver for EOF/HUP semantics, avoiding stale per-pair side counters.
int
pty_side_is_open(int pty_index, int side)
{
  int i;
  struct proc *p;
  struct file *f;

  if(pty_index < 0 || pty_index >= PTY_MAX_UNITS)
    return 0;
  if(side != PTY_SIDE_MASTER && side != PTY_SIDE_SLAVE)
    return 0;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == UNUSED || p->fdtable == 0)
      continue;

    for(i = 0; i < p->fdtable->nfds; i++) {
      f = p->fdtable->entries[i];
      if(f == 0)
        continue;
      if(f->ref < 1)
        continue;
      if(f->type != FD_INODE || f->ip == 0)
        continue;
      if(f->ip->type != T_DEV || f->ip->major != PTYDEV)
        continue;
      if(f->pty_index != pty_index)
        continue;
      if(f->pty_side != side)
        continue;

      release(&ptable.lock);
      return 1;
    }
  }

  release(&ptable.lock);
  return 0;
}

int
proc_has_cwd_on_dev(uint dev)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(inode_on_dev(p->cwd, dev)){
      release(&ptable.lock);
      return 1;
    }
  }
  release(&ptable.lock);
  return 0;
}

uint
proc_cwd_dev(void)
{
  struct proc *p;
  uint dev;

  dev = 0;
  acquire(&ptable.lock);
  p = myproc();
  if(p && p->cwd)
    dev = inode_get_dev(p->cwd);
  release(&ptable.lock);

  return dev;
}

struct inode*
proc_cwd_idup(void)
{
  struct proc *p;
  struct inode *ip;

  ip = 0;
  acquire(&ptable.lock);
  p = myproc();
  if(p && p->cwd)
    ip = idup(p->cwd);
  release(&ptable.lock);

  return ip;
}