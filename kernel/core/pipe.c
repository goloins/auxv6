#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "signal.h"

struct pipe {
  struct spinlock lock;
  char data[PIPE_CAPACITY];
  uint nread;     // number of bytes read
  uint nwrite;    // number of bytes written
  int readopen;   // read fd is still open
  int writeopen;  // write fd is still open
};

typedef char pipe_struct_size_guard[
  (sizeof(struct pipe) <= PGSIZE) ? 1 : -1
];

struct {
  struct spinlock lock;
  struct pipe *head;
  uint count;
} pipe_cache;

static volatile int pipe_cache_state; // 0=uninit,1=initing,2=ready

static volatile uint pipe_read_sleep_calls;
static volatile uint pipe_write_sleep_calls;
static volatile uint pipe_wakeup_readers;
static volatile uint pipe_wakeup_writers;

#define PIPE_CACHE_MAX 256

static void
pipe_cache_init_once(void)
{
  int s;

  s = pipe_cache_state;
  if(s == 2)
    return;
  if(s == 0 && __sync_bool_compare_and_swap(&pipe_cache_state, 0, 1)){
    initlock(&pipe_cache.lock, "pipe_cache");
    __sync_synchronize();
    pipe_cache_state = 2;
    return;
  }

  while(pipe_cache_state != 2)
    ;
}

void
pipe_get_stats(uint *read_sleeps,
               uint *write_sleeps,
               uint *wake_readers,
               uint *wake_writers)
{
  *read_sleeps = pipe_read_sleep_calls;
  *write_sleeps = pipe_write_sleep_calls;
  *wake_readers = pipe_wakeup_readers;
  *wake_writers = pipe_wakeup_writers;
}

int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *p;

  pipe_cache_init_once();

  p = 0;
  *f0 = *f1 = 0;
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad;
  acquire(&pipe_cache.lock);
  if(pipe_cache.head){
    p = pipe_cache.head;
    pipe_cache.head = *(struct pipe**)p;
    if(pipe_cache.count > 0)
      pipe_cache.count--;
  }
  release(&pipe_cache.lock);
  if(p == 0 && (p = (struct pipe*)kalloc()) == 0)
    goto bad;
  memset(p, 0, sizeof(*p));
  p->readopen = 1;
  p->writeopen = 1;
  p->nwrite = 0;
  p->nread = 0;
  initlock(&p->lock, "pipe");
  lockdep_set_rank(&p->lock, LOCK_RANK_DEFAULT, "pipe");
  (*f0)->type = FD_PIPE;
  (*f0)->readable = 1;
  (*f0)->writable = 0;
  (*f0)->pipe = p;
  (*f1)->type = FD_PIPE;
  (*f1)->readable = 0;
  (*f1)->writable = 1;
  (*f1)->pipe = p;
  return 0;

//PAGEBREAK: 20
 bad:
  if(p)
    kfree((char*)p);
  if(*f0)
    fileclose(*f0);
  if(*f1)
    fileclose(*f1);
  return -1;
}

void
pipeclose(struct pipe *p, int writable)
{
  acquire(&p->lock);
  if(writable){
    p->writeopen = 0;
    pipe_wakeup_readers++;
    wakeup(&p->nread);
  } else {
    p->readopen = 0;
    pipe_wakeup_writers++;
    wakeup(&p->nwrite);
  }
  if(p->readopen == 0 && p->writeopen == 0){
    release(&p->lock);
    pipe_cache_init_once();
    acquire(&pipe_cache.lock);
    if(pipe_cache.count < PIPE_CACHE_MAX){
      *(struct pipe**)p = pipe_cache.head;
      pipe_cache.head = p;
      pipe_cache.count++;
      release(&pipe_cache.lock);
    } else {
      release(&pipe_cache.lock);
      kfree((char*)p);
    }
  } else
    release(&p->lock);
}

//PAGEBREAK: 40
int
pipewrite(struct pipe *p, char *addr, int n)
{
  int i;
  int chunk;
  int space;
  int was_empty;
  uint off;
  uint until_wrap;
  struct proc *curproc = myproc();
  pde_t *pgdir;
  int user_src;

  user_src = ((uint)addr < KERNBASE);
  pgdir = user_src ? proc_pgdir(curproc) : 0;
  if(user_src && (curproc == 0 || pgdir == 0))
    return -1;

  acquire(&p->lock);
  
  // Check if read end is already closed - generate SIGPIPE
  if(p->readopen == 0) {
    release(&p->lock);
    curproc->sig_pending |= SIGBIT(SIGPIPE);
    return -1;
  }

  i = 0;
  while(i < n){
    while(p->nwrite == p->nread + PIPE_CAPACITY){  //DOC: pipewrite-full
      if(p->readopen == 0 || curproc->killed){
        release(&p->lock);
        if(p->readopen == 0)
          curproc->sig_pending |= SIGBIT(SIGPIPE);
        return -1;
      }
      pipe_write_sleep_calls++;
      pipe_wakeup_readers++;
      wakeup(&p->nread);
      sleep(&p->nwrite, &p->lock);  //DOC: pipewrite-sleep
    }

    was_empty = (p->nwrite == p->nread);

    space = PIPE_CAPACITY - (int)(p->nwrite - p->nread);
    chunk = n - i;
    if(chunk > space)
      chunk = space;

    off = p->nwrite % PIPE_CAPACITY;
    until_wrap = PIPE_CAPACITY - off;
    if((uint)chunk > until_wrap)
      chunk = (int)until_wrap;

    if(user_src){
      if(copyin(pgdir, &p->data[off], (uint)(addr + i), chunk) < 0){
        release(&p->lock);
        return (i > 0) ? i : -1;
      }
    } else {
      memmove(&p->data[off], addr + i, chunk);
    }

    p->nwrite += chunk;
    i += chunk;

    // Only wake readers when data transitions from empty to available.
    if(was_empty){
      pipe_wakeup_readers++;
      wakeup(&p->nread);
    }
  }
  release(&p->lock);
  return n;
}

int
piperead(struct pipe *p, char *addr, int n)
{
  int i;
  int chunk;
  int avail;
  int was_full;
  uint off;
  uint until_wrap;
  int user_dst;
  struct proc *curproc;
  pde_t *pgdir;

  user_dst = ((uint)addr < KERNBASE);
  curproc = user_dst ? myproc() : 0;
  pgdir = user_dst ? proc_pgdir(curproc) : 0;
  if(user_dst && (curproc == 0 || pgdir == 0))
    return -1;

  acquire(&p->lock);
  while(p->nread == p->nwrite && p->writeopen){  //DOC: pipe-empty
    if(myproc()->killed){
      release(&p->lock);
      return -1;
    }
    pipe_read_sleep_calls++;
    sleep(&p->nread, &p->lock); //DOC: piperead-sleep
  }
  i = 0;
  while(i < n){  //DOC: piperead-copy
    if(p->nread == p->nwrite)
      break;

    avail = (int)(p->nwrite - p->nread);
    chunk = n - i;
    if(chunk > avail)
      chunk = avail;

    was_full = (p->nwrite == p->nread + PIPE_CAPACITY);

    off = p->nread % PIPE_CAPACITY;
    until_wrap = PIPE_CAPACITY - off;
    if((uint)chunk > until_wrap)
      chunk = (int)until_wrap;

    if(user_dst){
      release(&p->lock);
      if(copyout(pgdir, (uint)(addr + i), &p->data[off], chunk) < 0)
        return (i > 0) ? i : -1;
      acquire(&p->lock);
    } else {
      memmove(addr + i, &p->data[off], chunk);
    }
    p->nread += chunk;
    i += chunk;

    // Only wake writers when space transitions from full to available.
    if(was_full){
      pipe_wakeup_writers++;
      wakeup(&p->nwrite);  //DOC: piperead-wakeup
    }
  }
  release(&p->lock);
  return i;
}

int
pipe_readable(struct pipe *p)
{
  int ready;

  acquire(&p->lock);
  ready = (p->nread != p->nwrite) || (p->writeopen == 0);
  release(&p->lock);
  return ready;
}

int
pipe_writable(struct pipe *p)
{
  int ready;

  acquire(&p->lock);
  ready = (p->nwrite < p->nread + PIPE_CAPACITY) || (p->readopen == 0);
  release(&p->lock);
  return ready;
}

int
pipe_writeopen(struct pipe *p)
{
  int open;

  acquire(&p->lock);
  open = p->writeopen;
  release(&p->lock);
  return open;
}
