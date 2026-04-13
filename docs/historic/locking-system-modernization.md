# Locking System Modernization Plan

## Implementation Status (2026-04-05)

This plan is now partially implemented in-tree, with critical stability goals
already landed and guest-validated.

### Landed Changes

1. Phase 1 safety nets are live:
   - `SPINLOCK_TIMEOUT_ITERS` is enforced in `acquire()` to prevent silent
     infinite spins.
   - nested acquire on the same lock is detected and panics immediately.
   - lock-failure diagnostics print lock name/CPU/owner info before panic.

2. File-table locking was modernized:
   - `ftable.lock` moved to sleeplock-based locking for file descriptor table
     operations (`filealloc`, `filedup`, `fileclose`, `file_has_refs_on_dev`).

3. Console locking was split into fine-grained locks:
   - `cons.input_lock` (spinlock): interrupt-context input path.
   - `cons.tty_lock` (spinlock): tty state paths used from syscall/kernel
     console output contexts.
   - `cons.gfx_lock` (spinlock): reserved for graphics register critical paths.

4. Lockdep-lite order validation is now in-tree:
  - per-lock rank metadata (`rank`, `class_name`) added to `struct spinlock`.
  - acquire-time order checks panic on rank inversion.
  - release-time checks panic on non-LIFO release order.
  - per-CPU held-lock chain diagnostics identify active lock stack on failure.
  - core lock-class annotations now cover additional hot-path locks (`icache`,
    `bcache`, `pipe`, `bdev`, `vfs`, `ktime`, `irq`) to improve panic
    readability and reduce `(null)`/implicit-class diagnostics.

5. Post-landing lockdep regression sweep is complete for the initial tranche:
   - guest-reported rank inversion (`ptable` -> `kmem`) was fixed by explicit
     allocator rank annotation.
   - a real console timed-read lock-order bug was fixed in `consoleread()` by
     releasing tty lock before acquiring `tickslock` in the timeout-wait path.
   - canonical xv6 sleep handoff behavior (`sleep(chan, lk)`) is now treated as
     a sanctioned lockdep transition instead of a generic non-LIFO violation.
   - a `halt` panic (`mycpu called with interrupts enabled`) was fixed by
     hardening lockdep-aware `cprintf()` logic to call `mycpu()` only when
     interrupts are already disabled.
   - non-xv6fs boot paths now initialize inode-cache locking unconditionally:
     `icache_init()` runs before VFS/bootstrap inode reference flows so
     `idup()` cannot acquire an uninitialized `icache.lock`.

Current manual guest validation status: `lockprobe` default, `-v`, and debug
variants pass without lockdep panic.

### Additional Hardening (Post-Validation)

- `lockprobe` gained a lockdep handoff selftest mode (`-L`) that exercises
  sanctioned sleep/wakeup lock transitions (`pipe` and `ticks` paths) as a
  regression tripwire for non-LIFO false positives and real lock-order bugs.
- Storage/network lock coverage was expanded with explicit lockdep class
  annotations across core net tables/socket layer, storage controller locks,
  major NIC drivers, and filesystem mount locks. This preserves behavior while
  improving lock-chain diagnostics during panic triage.

### Boot-Stability Follow-up (Graphical Target)

- A regression reproduced only on graphical boot (`make qemu`) exposed a
  validation gap from relying too heavily on `make qemu-nox` runs.
- Root cause was lockdep enforcement occurring too early in boot/device bringup
  paths; this could hard-fail before useful serial diagnostics were available.
- Resolution: lockdep runtime checks are now armed explicitly via
  `lockdep_enable()` after late boot initialization (`kinit2` and
  `console_gfx_late_enable()`), preserving runtime lockdep value while avoiding
  pre-console/pre-uart hard-fail risk.

### Required Validation Checklist (Lock/Console Changes)

Apply this checklist to any change in spinlock/sleeplock/lockdep logic,
console locking, tty read/write synchronization, or framebuffer mirror paths.

1. Host boot test: `sudo make qemu-nox`
2. Host boot test: `sudo make qemu`
3. Guest test: `lockprobe`
4. Guest test: `lockprobe -v`
5. Guest test: `lockprobe -D -C`
6. Guest test: `lockprobe -D -F`
7. Guest test: `lockprobe -L`
8. Guest test: `halt`

A change is not considered lock-safe until all items above pass.

### Important Design Deviation from Original Phase 2 Text

The original plan proposed converting console locking directly to sleeplock.
That was attempted and rolled back for early boot stability: console init and
early-console paths execute before full process/sleep infrastructure is safe for
that lock conversion. Console currently stays spinlock-based but is split into
separate domains to remove the worst lock-order coupling.

### Login Panic Postmortem (Resolved)

During the lock split, a regression caused:

`spinlock bad release: lock=console_tty cpu=0 owner_cpu=-1 locked=0`

Root causes were lock-pair mismatches introduced while refactoring:

- `consoleintr()` acquired `cons.input_lock` but released `cons.tty_lock`.
- one gfx ownership path acquired `cons.tty_lock` but released
  `cons.input_lock`.

Both mismatches were fixed. Current guest status confirms boot/login stability.

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

### Delivered Milestones

1. **Phase 1 (safety nets) - completed**
  - [x] Spinlock timeout panic path added.
  - [x] Nested same-lock acquire panic path added.
  - [x] Lock-failure diagnostics emitted before panic.
  - [x] Guest-validated: no silent lock hangs in tested login paths.

2. **Phase 2 (sleepable locks) - partially completed**
  - [x] Sleeplock infrastructure exists and is in active use.
  - [x] File table moved to sleeplock.
  - [ ] Console path moved to sleeplock (deferred; see deviation above).

3. **Phase 3 (fine-grained console split) - completed**
  - [x] Console split into input/tty/gfx lock domains.
  - [x] Interrupt input path uses dedicated input lock.
  - [x] Lock-pairing regressions found and fixed with diagnostics.

### Remaining Follow-On Work

1. Restrict `cons.gfx_lock` to strict register-submit windows and keep
  framebuffer prep outside that lock.
2. Evaluate per-tty lock granularity when `CONSOLE_NTTY` expands.
3. Expand rank annotation coverage to additional subsystems beyond the current
  core set (`console_*`, `ftable_internal`, `ticks`, `ptable`, `log`).
4. Gather performance counters under heavy console + signal workload after the
  split to quantify contention reduction.

## Configuration and Debug Flags

### Current Spinlock Debug Controls

```c
// In include/param.h
#define SPINLOCK_TIMEOUT_ITERS    100000000  // ~100ms at 1GHz
#define KDEBUG_SPINLOCK_LOCKFAIL  1          // pre-panic lock diagnostics
#define KDEBUG_LOCKDEP            1          // lock-order/release-order checks

// In kernel/core/spinlock.c
#if KDEBUG_SPINLOCK_LOCKFAIL
  cprintf("spinlock timeout: lock=%s cpu=%d owner_cpu=%d iter=%u\n", ...);
#endif
  if(iter >= SPINLOCK_TIMEOUT_ITERS) {
    panic("DEADLOCK: spinlock acquire timeout - circular lock dependency");
  }

#if KDEBUG_LOCKDEP
  // acquire-time rank inversion check + release-order validation
  // with per-CPU held-lock chain diagnostics
#endif
```

### Sleeplock Controls

```c
// docs/DEBUG-FLAGS.md entry
| KDEBUG_SLEEPLOCK | Log sleeplock acquire/release with caller info |

// In kernel/core/sleeplock.c
#ifdef KDEBUG_SLEEPLOCK
  cprintf("sleeplock_acquire: %s (caller %p)\n", sl->name, __builtin_return_address(0));
#endif
```

## Success Criteria

### Phase 1
- [x] System boots without timeout panics on normal operations
- [x] Timeout path exists and panics instead of silent spinning
- [x] Lock-failure diagnostics include lock name and owner CPU
- [x] Serial output shows deadlock diagnostics

### Phase 2
- [ ] All console operations work with sleeplock (deferred)
- [x] Ctrl-C no longer reproduces the original silent lock hang in tested flows
- [x] Login with invalid usernames no longer hangs in validated sessions
- [x] No stability regression after lock-pair fixes

### Phase 3
- [x] Console lock split into input/gfx/tty domains
- [x] Interrupt handler holds only `input_lock`
- [ ] Multi-vCPU contention benchmark data still pending
- [ ] Latency characterization under load still pending

### Lockdep-Lite Follow-On
- [x] Rank-aware lock metadata added to spinlock core
- [x] Acquire-time lock order checks enabled (`KDEBUG_LOCKDEP`)
- [x] Release-order checks enabled (`KDEBUG_LOCKDEP`)
- [x] Core lock classes ranked and annotated
- [ ] Rank coverage expanded for the rest of kernel lock classes

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
