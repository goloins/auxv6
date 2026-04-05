// Mutual exclusion spin locks.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
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

