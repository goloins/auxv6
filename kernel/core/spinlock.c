// Mutual exclusion spin locks.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"

#if KDEBUG_LOCKDEP
static int lockdep_runtime_enabled;

void
lockdep_enable(void)
{
  lockdep_runtime_enabled = 1;
}

static int
lockdep_is_ptable(struct spinlock *lk)
{
  if(lk == 0)
    return 0;
  if(lk->class_name && strcmp(lk->class_name, "ptable") == 0)
    return 1;
  if(lk->name && strcmp(lk->name, "ptable") == 0)
    return 1;
  return 0;
}

static void
lockdep_dump_chain(struct cpu *c)
{
  int i;

  cprintf("lockdep held chain (depth=%d):\n", c ? c->lockdep_depth : -1);
  if(c == 0)
    return;
  for(i = 0; i < c->lockdep_depth; i++) {
    struct spinlock *lk = c->lockdep_locks[i];
    cprintf("  [%d] %s class=%s rank=%d\n",
            i,
            lk && lk->name ? lk->name : "(null)",
            lk && lk->class_name ? lk->class_name : "(null)",
            c->lockdep_ranks[i]);
  }
}

static void
lockdep_on_acquire(struct spinlock *lk)
{
  struct cpu *c;
  int top_rank;

  c = mycpu();
  if(c == 0)
    return;

  if(c->lockdep_depth > 0) {
    top_rank = c->lockdep_ranks[c->lockdep_depth - 1];
    // ptable is a special synchronization pivot in xv6 sleep/sched paths:
    // allow acquiring ptable under other locks and acquiring other locks
    // while ptable is held in narrowly-scoped kernel paths.
    if(!lockdep_is_ptable(lk) &&
       !lockdep_is_ptable(c->lockdep_locks[c->lockdep_depth - 1]) &&
       lk->rank < top_rank) {
      cprintf("lockdep order violation: acquire %s(class=%s rank=%d) while holding rank=%d\n",
              lk->name ? lk->name : "(null)",
              lk->class_name ? lk->class_name : "(null)",
              lk->rank,
              top_rank);
      lockdep_dump_chain(c);
      panic("lockdep: order violation");
    }
  }

  if(c->lockdep_depth >= MAX_LOCKDEP_HELD) {
    cprintf("lockdep overflow: cpu=%d depth=%d max=%d\n",
            c->apicid,
            c->lockdep_depth,
            MAX_LOCKDEP_HELD);
    panic("lockdep: overflow");
  }

  c->lockdep_locks[c->lockdep_depth] = lk;
  c->lockdep_ranks[c->lockdep_depth] = lk->rank;
  c->lockdep_depth++;
}

static void
lockdep_on_release(struct spinlock *lk)
{
  struct cpu *c;
  int idx;
  int i;

  c = mycpu();
  if(c == 0)
    return;

  if(c->lockdep_depth <= 0) {
    cprintf("lockdep underflow: release %s(class=%s rank=%d) with empty chain\n",
            lk->name ? lk->name : "(null)",
            lk->class_name ? lk->class_name : "(null)",
            lk->rank);
    panic("lockdep: underflow");
  }

  idx = c->lockdep_depth - 1;
  if(c->lockdep_locks[idx] != lk) {
    // Allow xv6 sleep handoff pattern: sleep(chan, lk) acquires ptable,
    // then releases lk while ptable remains held.
    if(c->lockdep_depth >= 2 &&
       c->lockdep_locks[c->lockdep_depth - 1] &&
       c->lockdep_locks[c->lockdep_depth - 1]->name &&
       strcmp(c->lockdep_locks[c->lockdep_depth - 1]->name, "ptable") == 0 &&
       c->lockdep_locks[c->lockdep_depth - 2] == lk) {
      c->lockdep_depth--;
      c->lockdep_locks[c->lockdep_depth - 1] = c->lockdep_locks[c->lockdep_depth];
      c->lockdep_ranks[c->lockdep_depth - 1] = c->lockdep_ranks[c->lockdep_depth];
      c->lockdep_locks[c->lockdep_depth] = 0;
      c->lockdep_ranks[c->lockdep_depth] = 0;
      return;
    }

    // If a lock appears below the stack top, this is almost always a bug.
    // Keep a clear diagnostic with the held-lock chain.
    for(i = c->lockdep_depth - 1; i >= 0; i--) {
      if(c->lockdep_locks[i] == lk)
        break;
    }
    cprintf("lockdep release order violation: releasing %s(class=%s rank=%d), top is %s(class=%s rank=%d)\n",
            lk->name ? lk->name : "(null)",
            lk->class_name ? lk->class_name : "(null)",
            lk->rank,
            c->lockdep_locks[idx] && c->lockdep_locks[idx]->name ? c->lockdep_locks[idx]->name : "(null)",
            c->lockdep_locks[idx] && c->lockdep_locks[idx]->class_name ? c->lockdep_locks[idx]->class_name : "(null)",
            c->lockdep_ranks[idx]);
    if(i >= 0)
      cprintf("lockdep note: release target present at depth index %d\n", i);
    lockdep_dump_chain(c);
    panic("lockdep: release order violation");
  }

  c->lockdep_depth--;
  c->lockdep_locks[c->lockdep_depth] = 0;
  c->lockdep_ranks[c->lockdep_depth] = 0;
}
#endif

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->rank = LOCK_RANK_DEFAULT;
  lk->class_name = name;
  lk->cpu = 0;
}

void
lockdep_set_rank(struct spinlock *lk, int rank, char *class_name)
{
  if(lk == 0)
    return;
  lk->rank = rank;
  if(class_name)
    lk->class_name = class_name;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
// Holding a lock for a long time may cause
// other CPUs to waste time spinning to acquire it.
void
acquire(struct spinlock *lk)
{
  uint iter;

  pushcli(); // disable interrupts to avoid deadlock.
  if(holding(lk)) {
#if KDEBUG_SPINLOCK_LOCKFAIL
    cprintf("spinlock acquire nested: lock=%s cpu=%d\n",
            lk->name ? lk->name : "(null)",
            mycpu() ? mycpu()->apicid : -1);
#endif
    panic("acquire: deadlock - nested acquire of same lock");
  }

  // The xchg is atomic.
  // The 'pause' hint tells the CPU this is a spin-wait loop, which
  // reduces bus traffic and power consumption on HT/SMT cores and
  // avoids a memory-order violation penalty when the lock is released.
  iter = 0;
  while(xchg(&lk->locked, 1) != 0) {
    iter++;
    asm volatile("pause");

    // Spinlock timeout watchdog: if we've been spinning for way too long,
    // diagnose and panic. This prevents silent deadlocks - the kernel will
    // now panic with a clear message instead of hanging silently.
    if(iter >= SPINLOCK_TIMEOUT_ITERS) {
      // Panic with diagnostic info. With KDEBUG_SPINLOCK_CALLSTACK enabled,
      // the stack trace shows which code is stuck trying to acquire this lock.
#if KDEBUG_SPINLOCK_LOCKFAIL
      cprintf("spinlock timeout: lock=%s cpu=%d owner_cpu=%d iter=%u\n",
              lk->name ? lk->name : "(null)",
              mycpu() ? mycpu()->apicid : -1,
              lk->cpu ? lk->cpu->apicid : -1,
              iter);
#endif
      panic("DEADLOCK: spinlock acquire timeout - circular lock dependency");
    }
  }

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen after the lock is acquired.
  __sync_synchronize();

  // Record info about lock acquisition for debugging.
  lk->cpu = mycpu();
#ifdef KDEBUG_SPINLOCK_CALLSTACK
  getcallerpcs(&lk, lk->pcs);
#endif
#if KDEBUG_LOCKDEP
  if(lockdep_runtime_enabled)
    lockdep_on_acquire(lk);
#endif
}

// Release the lock.
void
release(struct spinlock *lk)
{
  if(!holding(lk)) {
#if KDEBUG_SPINLOCK_LOCKFAIL
    cprintf("spinlock bad release: lock=%s cpu=%d owner_cpu=%d locked=%d\n",
            lk->name ? lk->name : "(null)",
            mycpu() ? mycpu()->apicid : -1,
            lk->cpu ? lk->cpu->apicid : -1,
            lk->locked);
#endif
    panic("release");
  }

#if KDEBUG_LOCKDEP
  if(lockdep_runtime_enabled)
    lockdep_on_release(lk);
#endif

  lk->pcs[0] = 0;
  lk->cpu = 0;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other cores before the lock is released.
  // Both the C compiler and the hardware may re-order loads and
  // stores; __sync_synchronize() tells them both not to.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code can't use a C assignment, since it might
  // not be atomic. A real OS would use C atomics here.
  asm volatile("movl $0, %0" : "+m" (lk->locked) : );

  popcli();
}

// Record the current call stack in pcs[] by following the %ebp chain.
void
getcallerpcs(void *v, uint pcs[])
{
  uint *ebp;
  int i;

  ebp = (uint*)v - 2;
  for(i = 0; i < 10; i++){
    if(ebp == 0 || ebp < (uint*)KERNBASE || ebp == (uint*)0xffffffff)
      break;
    pcs[i] = ebp[1];     // saved %eip
    ebp = (uint*)ebp[0]; // saved %ebp
  }
  for(; i < 10; i++)
    pcs[i] = 0;
}

// Check whether this cpu is holding the lock.
int
holding(struct spinlock *lock)
{
  int r;
  pushcli();
  r = lock->locked && lock->cpu == mycpu();
  popcli();
  return r;
}


// Pushcli/popcli are like cli/sti except that they are matched:
// it takes two popcli to undo two pushcli.  Also, if interrupts
// are off, then pushcli, popcli leaves them off.

void
pushcli(void)
{
  int eflags;

  eflags = readeflags();
  cli();
  if(mycpu()->ncli == 0)
    mycpu()->intena = eflags & FL_IF;
  mycpu()->ncli += 1;
}

void
popcli(void)
{
  if(readeflags()&FL_IF)
    panic("popcli - interruptible");
  if(--mycpu()->ncli < 0)
    panic("popcli");
  if(mycpu()->ncli == 0 && mycpu()->intena)
    sti();
}

