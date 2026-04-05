# Locking System Modernization Plan

## Current State: Single-Level Spinlock Architecture

auxv6 currently uses a **single-level spinlock system** for all mutual exclusion:

```c
struct spinlock lock;

acquire(&lock);           // Disable interrupts, spin-wait for lock
  // critical section
release(&lock);           // Re-enable interrupts
```

**Architecture:**
- Spinlock disables interrupts locally via `cli()` / `sti()`
- Busy-wait spin loop until lock acquired
- No timeout; infinite spin possible if lock holder is blocked
- No deadlock detection or prevention
- No concept of waiting/sleeping threads

**Where spinlocks are used:**
- `cons.lock` - console I/O, graphics, signal delivery, TTY operations
- `tickslock` - timer interrupt synchronization
- `ftable.lock` - file table synchronization
- `log.lock` - disk block logging
- Per-inode locks in filesystem operations

## Problems with Single-Level Spinlocks

### 1. Silent Deadlocks with Infinite Spin

When a deadlock occurs (circular dependencies), the CPU spins forever:
- Interrupts disabled → timer IRQ never fires
- No UART ISR → no diagnostic output possible
- No progress → silent hang with no diagnostic

**Example from recent bugs:**
```
User types "oot" at login prompt
→ login process tries to read password, acquires cons.lock
→ signal handler fires (Ctrl-C), tries to acquire cons.lock
→ CPU spins forever with interrupts disabled
→ Zero UART output, system appears dead
```

### 2. Lock Hold Time Critical

Spinlocks must hold locks for **milliseconds or less**. Currently:
- `console.lock` holds lock while comparing 100KB of framebuffer data
- `console.lock` holds lock while calling virtio-gpu driver (device register I/O)
- `console.lock` holds lock during signal delivery and process table traversal
- `log.lock` holds lock while waiting for disk I/O (potentially seconds!)

**Result:** Lock contention, CPU waste, priority inversion.

### 3. Signal-Safety Hazards

Interrupt handlers and signal delivery code must be lock-safe but interact badly with spinlocks:

```c
// Scenario: interrupt handler can't safely print to console
consoleintr() {
  // IRQ context with interrupts disabled
  acquire(&cons.lock);  // DEADLOCK if main code already holds this
                         // OR another CPU holds it
}
```

Current workaround: deferred signal delivery, null-safe curproc caching. But this is symptom management, not root cause fix.

### 4. No Timeout or Watchdog Mechanism

If a lock holder crashes or deadlocks:
- No automatic timeout
- No watchdog to detect infinite loops
- No deadlock reporting
- Silent system failure

## Modern Unix Locking Hierarchy

Linux, FreeBSD, and NetBSD all use a **layered locking architecture**:

### Layer 1: Spinlocks (Hardware Register Access)

**Purpose:** Protect operations that must run with precise timing or in interrupt context.

**Constraints:**
- Hold time: microseconds (< 1ms)
- Can't block, context-switch, or call I/O operations
- Used for: CPU registers, per-CPU structures, interrupt handlers

**FreeBSD API:**
```c
mtx_lock_spin(&lock);         // Acquires spinlock, disables interrupts
  // critical section (microseconds)
mtx_unlock_spin(&lock);
```

### Layer 2: Sleepable Mutexes (Default for Most Code)

**Purpose:** General-purpose mutual exclusion for operations that might wait.

**Design:**
```c
if(can't_acquire_lock) {
  // Don't spin - yield to another runnable thread
  context_switch_to_another_thread();
  // Wake up when lock becomes available
}
```

**Guarantees:**
- CPU still makes progress (another thread runs)
- Interrupts stay enabled (handler can run without special care)
- Can safely call I/O operations, syscalls, sleep
- Priority inheritance prevents priority inversion

**FreeBSD API:**
```c
mtx_lock(&mutex);             // May block/context-switch if contended
  // critical section (can be long - file I/O, syscalls, etc.)
mtx_unlock(&mutex);
```

**Key Safety Property:**
```c
// Main code
mtx_lock(&mutex);  // Can block, but won't deadlock if...
  call_signal_handler();

// Signal handler (interrupt context)
if(mtx_trylock(&mutex)) {    // Non-blocking attempt
  // safe section
  mtx_unlock(&mutex);
} else {
  // Can't get lock, but that's OK - main code will release it
  // (because interrupts are enabled during mtx_lock hold)
}
```

### Layer 3: Reader-Writer Locks

**Purpose:** For read-mostly data where many readers are safe but writers need exclusion.

**Example:** Process table (mostly read for access, rarely modified).

```c
rwlock_t lock;

// Readers
read_lock(&lock);
  // Many threads can hold read lock simultaneously
  access_data();
read_unlock(&lock);

// Writers
write_lock(&lock);
  // Exclusive access
  modify_data();
write_unlock(&lock);
```

### Layer 4: Deadlock Detection (Witness/Lock Validation)

Modern kernels include **static deadlock detection** tools:

**FreeBSD's Witness:**
- Tracks lock acquisition order everywhere
- Detects if different code paths acquire the same locks in different orders
- Refuses to acquire a lock if it would create a cycle
- Reports lock ordering violations with call stacks

**Benefits:**
- Catches deadlock bugs at development time, not production
- Prevents priority inversion
- Documents intended lock hierarchy

## Case Study: Console Locking in auxv6 vs. FreeBSD

### Current auxv6 Console Lock

```c
// kernel/driver/console.c
acquire(&cons.lock);        // Spinlock, interrupts disabled
  consoleread() {
    // Read from TTY buffer (microseconds)
    // BUT ALSO:
    acquire(&tickslock);    // Nested lock!
    // Read termios flags (microseconds)
    release(&tickslock);
    
    // Call signal delivery (process table traversal!)
    proc_signal_pgid(pgid, SIGTTIN);
      // Walks process table while cons.lock held
      // Might trigger memory allocation
  }
  
  console_gfx_sync_from_tty_locked() {
    // Copy 100KB of framebuffer data
    // Wait for virtio-gpu device (microseconds to milliseconds)
    // Signal delivery defers output
  }
release(&cons.lock);
```

**Problems:**
- Lock held during signal delivery (potential deadlock)
- Lock held during framebuffer operations (CPU waste)
- Lock held during process table traversal (priority inversion risk)
- No way to distinguish "register access" from "policy decision"

### FreeBSD TTY Subsystem (Model)

```c
// Two-lock strategy
mtx_lock(&tty->lock);           // Sleep mutex - main TTY operations
  {
    // TTY buffer management (milliseconds)
    read_from_buffer();
    
    // Deferred signal delivery (outside lock)
  }
mtx_unlock(&tty->lock);

// Signal delivery happens with tty->lock NOT held
// If it needs to access TTY, it reacquires lock non-blocking

// Spinlock only for:
if(need_to_read_hardware_register) {
  mtx_lock_spin(&hw_lock);      // Spinlock - hardware control only
    read_register();
  mtx_unlock_spin(&hw_lock);
}
```

**Advantages:**
- Signals can interrupt without deadlock risk
- Framebuffer operations don't block meaningful progress
- GFX operations can happen without holding TTY lock
- Lock hold time is predictable and short

## Observed Bugs Caused by Single-Level Locking

### Bug 1: Ctrl-C Silent Crash (Fixed with Deferred Signal Delivery)

**Root cause:** Signal handler tried to acquire console lock while it was already held.
- CPU in main code holds `cons.lock` with interrupts disabled
- Ctrl-C fires interrupt, signal handler tries to acquire same lock
- CPU spins forever, deadlock

**Symptom:** System hangs, random crash/reboot when sending Ctrl-C under heavy output.

**Current fix (workaround):** Defer signal delivery outside lock.

**Real fix (would require sleepable locks):** Signal delivery doesn't need spinlock at all; use sleepable mutex.

### Bug 2: Silent Login Hang ("oot" Username Typo)

**Root cause:** Unknown deadlock in login path (not yet diagnosed).
- Login process reads username, tries console operations
- Possible circular dependency with signal delivery or process management
- CPU spins with interrupts disabled, zero UART output

**Symptom:** System completely silent when invalid username entered.

**Current workaround:** Spinlock timeout watchdog (detects hang but doesn't prevent it).

**Real fix:** Sleepable locks break circular dependencies by allowing context switches.

### Bug 3: Lock Contention Under Heavy Output

**Root cause:** `console.lock` held for milliseconds during framebuffer operations.
- One CPU doing console operations holds lock
- Other CPUs spin waiting (wasting cycles)
- Interactive response degrades

**Symptom:** System sluggish under heavy terminal output.

**Current workaround:** Virtio-GPU paired command submission reduces overhead.

**Real fix:** Separate locks for TTY buffer (sleep mutex) and graphics output (spinlock).

## Three-Phase Modernization Plan

### Phase 1: Safety Nets (Immediate, No Architectural Changes)

**Goal:** Prevent silent hangs, add visibility into deadlocks.

**Changes:**
1. **Spinlock acquire timeout:** After N iterations, panic with diagnostic info
2. **Deadlock detection tool:** Detect and warn about circular lock dependencies
3. **Lock order validation:** Assert that locks are always acquired in same order

**Implementation:**
```c
// In kernel/core/spinlock.c
void acquire(struct spinlock *lk) {
  uint iterations = 0;
  while(xchg(&lk->locked, 1) != 0) {
    iterations++;
    if(iterations > SPINLOCK_TIMEOUT) {
      panic("DEADLOCK: spinlock %s exceeded timeout", lk->name);
    }
  }
}
```

**Benefits:**
- No more silent hangs - deadlocks produce panic with stack trace
- Can diagnose which lock is causing hang
- No architecture changes, compatible with existing code
- Provides evidence for Phase 2 migration

**Time estimate:** 1-2 hour implementation

### Phase 2: Sleepable Locks Infrastructure (Short-term)

**Goal:** Introduce `sleeplock` primitive for operations that need to wait.

**Changes:**
1. **New sleeplock.h:** Implement sleep mutex (similar to freebsd/netbsd)
2. **Update console lock:** Change `cons.lock` from spinlock to sleeplock
3. **Update file locks:** Change `ftable.lock` from spinlock to sleeplock
4. **Verify signal safety:** Ensure signal handlers can interrupt sleeplocks

**Implementation approach:**

```c
// New include/sleeplock.h
struct sleeplock {
  uint locked;
  struct spinlock lk;      // Protect waiters list
  struct proc *waiters;    // List of waiting procs
};

void sleeplock_acquire(struct sleeplock *sl) {
  acquire(&sl->lk);
  while(sl->locked) {
    sleep(sl, &sl->lk);    // Release lk, sleep, re-acquire lk
  }
  sl->locked = 1;
  release(&sl->lk);
}

void sleeplock_release(struct sleeplock *sl) {
  acquire(&sl->lk);
  sl->locked = 0;
  wakeup(sl);
  release(&sl->lk);
}
```

**Console lock refactor:**

```c
// Before (spinlock - always holds interrupts disabled)
acquire(&cons.lock);
  consoleread();
release(&cons.lock);

// After (sleeplock - interrupts enabled)
sleeplock_acquire(&cons.lock);
  consoleread();
sleeplock_release(&cons.lock);

// Signal handlers can now interrupt safely
void consoleintr() {
  // Don't need to acquire cons.lock
  // Just add to input buffer
  cons.input.buf[...] = c;
}
```

**Benefits:**
- Signals can interrupt without deadlock
- Long-running operations don't hold lock exclusively
- Framebuffer operations are independent from TTY operations
- CPUs don't waste time spinning
- Compatible with syscall/I/O operations

**Time estimate:** 4-6 hours implementation + testing

### Phase 3: Fine-Grained Locking (Medium-term)

**Goal:** Split locks by operation type - spinlocks only for register access.

**Changes:**
1. **Input spinlock:** Protect just the input buffer (IRQ handler needs it)
2. **Output sleeplock:** Protect main TTY state (can wait for I/O)
3. **Graphics spinlock:** Protect virtio-gpu device registers (short hold)
4. **Process table sleeplock:** Protect process list (separate from console)

**Example architecture:**

```c
struct console_state {
  struct spinlock input_lock;      // Just for input buffer add
  struct sleeplock tty_lock;       // TTY operations
  struct spinlock gfx_reg_lock;    // Graphics register access
};

// Interrupt handler - only needs input_lock
void consoleintr() {
  acquire(&cons.input_lock);
  cons.input.buf[cons.input.w % INPUT_BUF] = c;
  cons.input.w++;
  release(&cons.input_lock);
}

// Main read/write - uses tty_lock
void consoleread() {
  sleeplock_acquire(&cons.tty_lock);
  // Can wait for data, call I/O, etc.
  sleeplock_release(&cons.tty_lock);
}

// Graphics flush - short-lived spinlock for registers
void console_gfx_sync() {
  // Long framebuffer prep with no lock
  prepare_framebuffer_data();
  
  // Brief register access with spinlock
  acquire(&cons.gfx_reg_lock);
  virtio_gpu_submit();
  release(&cons.gfx_reg_lock);
}
```

**Benefits:**
- Minimal contention (input/gfx overlap safely)
- Signals can interrupt main code paths
- Framebuffer operations parallel with TTY operations
- Clear separation of concerns

**Time estimate:** 8-12 hours implementation + integration

## Implementation Roadmap

### Immediate (This Week)

1. **Implement Phase 1 (safety nets)**
   - [ ] Add spinlock timeout with panic diagnostic
   - [ ] Add basic deadlock detection (detect nested acquire of same lock)
   - [ ] Test with known deadlock scenarios
   - [ ] Document in code with config flag `KDEBUG_SPINLOCK_TIMEOUT`

2. **Testing**
   - [ ] Boot system, verify timeout doesn't fire on normal operations
   - [ ] Trigger deadlock scenario, verify panic message is informative
   - [ ] Check serial output for diagnostic information

### Short-term (Weeks 2-3)

1. **Implement Phase 2 (sleepable locks)**
   - [ ] Create `include/sleeplock.h` with `sleeplock_acquire/release`
   - [ ] Update `console.c` to use sleeplock instead of spinlock
   - [ ] Update file table to use sleeplock
   - [ ] Update process locks as necessary
   
2. **Validation**
   - [ ] Boot and verify interactive operations work
   - [ ] Test signal delivery during console operations
   - [ ] Verify no regressions in framebuffer performance

### Medium-term (Weeks 4-6)

1. **Implement Phase 3 (fine-grained locks)**
   - [ ] Split console lock into input + tty + gfx
   - [ ] Verify no contention between independent operations
   - [ ] Performance profiling (latency, throughput)
   
2. **Documentation**
   - [ ] Update locking documentation with new primitives
   - [ ] Add examples showing proper sleeplock usage
   - [ ] Document which operations use which locks

## Configuration and Debug Flags

### For Phase 1 (Immediate)

```c
// In include/param.h
#define KDEBUG_SPINLOCK_TIMEOUT   1      // Enable spinlock timeout
#define SPINLOCK_TIMEOUT_ITERS    100000000  // ~50ms at 1GHz

// In kernel/core/spinlock.c
#ifdef KDEBUG_SPINLOCK_TIMEOUT
  if(iterations > SPINLOCK_TIMEOUT_ITERS) {
    panic("SPINLOCK TIMEOUT: %s held too long\n", lk->name);
  }
#endif
```

### For Phase 2 (Debug sleeplocks)

```c
// config/debug-flags.md entry
| KDEBUG_SLEEPLOCK | Log sleeplock acquire/release with caller info |

// In kernel/core/sleeplock.c
#ifdef KDEBUG_SLEEPLOCK
  cprintf("sleeplock_acquire: %s (caller %p)\n", sl->name, __builtin_return_address(0));
#endif
```

## Success Criteria

### Phase 1
- [ ] System boots without timeout panics on normal operations
- [ ] Artificial deadlock triggers timeout panic (not silent hang)
- [ ] Panic message includes lock name and stack trace
- [ ] Serial output shows deadlock diagnostics

### Phase 2
- [ ] All console operations work with sleeplock
- [ ] Ctrl-C doesn't deadlock even under heavy output
- [ ] Login with any username works without hanging
- [ ] No performance regression

### Phase 3
- [ ] Independent operations (input/gfx/tty) don't block each other
- [ ] Interrupt handler only holds input_lock (brief)
- [ ] Multiple vcpus show improved parallelism
- [ ] Latency under load is acceptable

## References

**Linux Kernel:**
- Documentation/locking/spinlocks.rst
- Documentation/locking/mutexes.rst
- Documentation/locking/deadlocks.rst

**FreeBSD:**
- man 9 mutex (sleepable mutexes)
- man 9 mtx_lock_spin (spinlocks)
- src/sys/kern/kern_mutex.c (implementation)

**NetBSD:**
- man kmutex (kernel mutexes)
- man krwlock (reader-writer locks)

**Design Inspiration:**
- Unix kernel synchronization primitives
- Priority inheritance and priority inversion concepts
- Lock ordering and deadlock prevention
