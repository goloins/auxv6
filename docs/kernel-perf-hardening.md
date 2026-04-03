# Kernel Performance Hardening — auxv6

**Date:** 2026-04-03  
**Scope:** Kernel-space; no userland ABI changes  
**Status:** Landed, builds clean  

---

## Background and Motivation

auxv6 traces its lineage to xv6, a teaching OS intentionally kept minimal.
As the codebase has grown — more drivers, a multi-backend VFS, networking,
PTY support, NFS, and a larger userland — several xv6-origin design choices
have become measurable bottlenecks.  The most visible symptoms are:

- Sluggish interactive shell response under modest filesystem load
- Noticeable CPU spinning on multi-core QEMU runs even when idle
- Every `open()`, `stat()`, and `exec()` touching a global spinlock far more
  times than necessary

This document describes ten targeted changes, their rationale, and the
remaining opportunities that were deferred.

---

## Changes Landed

### 1. System-Wide Resource Limit Increases (`include/param.h`)

| Constant | Old | New | Bottleneck removed |
|----------|-----|-----|--------------------|
| `NPROC`  | 64  | 128 | Process table exhaustion under modest load |
| `NFILE`  | 100 | 256 | System-wide open-file ceiling; `filealloc()` O(100) scan |
| `NINODE` | 50  | 200 | Inode cache eviction under VFS-heavy workloads |
| `NOFILE` | 16  | 32  | Per-process fd ceiling; trivially hit by any real shell |
| `NBUF`   | 30  | 128 | **15 KB** of block cache for an entire OS — the worst inherited constant |

`NBUF` was coupled to `LOGSIZE` (`MAXOPBLOCKS*3 = 30`).  They are now
independent.  The log size is unchanged; only the cache grows.

`NBUF=30` deserves particular emphasis.  At `BSIZE=512` bytes the entire
buffer cache was 15 KB.  Nearly every filesystem operation evicted a live
buffer and triggered synchronous disk I/O.  128 buffers = 64 KB cache —
still tiny by modern standards but no longer a constant bottleneck.

---

### 2. `kfree()` Debug Poison Gating (`kernel/core/kalloc.c`)

**Problem:** `memset(v, 1, PGSIZE)` — a 4096-byte write — was called
unconditionally on every freed page.  `exec()` frees the entire old address
space page-by-page under `kmem.lock`, so every exec issued hundreds of
4 KB writes with the global allocator lock held.

**Fix:** The memset is now compiled in only when `-DKDEBUG_KFREE_POISON` is
passed to the compiler.  Production builds (without the flag) skip it
entirely.

**Risk:** Dangling-pointer bugs that the poison previously caught will no
longer fault immediately.  Add `-DKDEBUG_KFREE_POISON` to `CFLAGS` in
`Makefile` when debugging memory corruption.

---

### 3. Spinlock `pause` + Conditional Call-Stack Walk (`kernel/core/spinlock.c`)

**Two independent improvements:**

**a) `pause` in the spin loop:**
```c
while(xchg(&lk->locked, 1) != 0)
    asm volatile("pause");   // was: ;  (empty)
```
The x86 `PAUSE` instruction signals to the CPU that the current code is in a
spin-wait loop.  On HT/SMT cores this avoids a memory-order violation
penalty when the lock owner releases while a sibling logical CPU holds a
speculative read of the lock word.  It also reduces bus-lock traffic.
Observable improvement on QEMU `-smp 2+` configurations.

**b) `getcallerpcs()` made conditional:**
Before this fix, every `acquire()` in the kernel walked up to 10 `%ebp`
frames to record a call stack for debugging.  This O(10) pointer chain walk
executed on every lock acquisition — including the innermost hot paths in
`bcache`, `icache`, the scheduler, and `kalloc`.

The call-stack capture is now conditional on `-DKDEBUG_SPINLOCK_CALLSTACK`.
The `pcs[]` array in `struct spinlock` is retained (it is used by the
`holding()` debug check and may be useful in debuggers) but is only filled
when the flag is set.

---

### 4. O(1) `mycpu()` via APIC Reverse Map (`kernel/driver/mp.c`, `kernel/core/proc.c`)

**Problem:** `mycpu()` was called from `acquire()`, `release()`, `sched()`,
`yield()`, `myproc()`, and every process-table operation — i.e. on every
kernel entry and lock operation.  Each call:
1. Did an LAPIC MMIO read (`lapicid()` → read from `lapic[0x0020/4]`)
2. Walked `cpus[0..ncpu-1]` comparing APIC IDs (O(NCPU))

**Fix:** `mpinit()` builds a 256-entry `apic_cpu_map[]` array (APIC IDs are
single-byte values) mapping each APIC ID directly to its `cpus[]` index.
`mycpu()` becomes:
```c
idx = apic_cpu_map[(uchar)lapicid()];
return &cpus[idx];
```
One MMIO read, one array dereference.  The comment in the original source
(`"Maybe we should have a reverse map"`) is now resolved.

---

### 5. `struct cpu` Scan-Hint Field (`include/proc.h`)

Added `int sched_last` to `struct cpu`.  Each CPU stores the index of the
slot immediately past the last process it ran.  Used by the scheduler (§6)
to spread load across the table.

---

### 6. Scheduler: Per-CPU Scan Offset + Idle `hlt` (`kernel/core/proc.c`)

Two changes to `scheduler()`:

**a) Per-CPU scan start offset:**
Each CPU starts its next scheduling scan at `c->sched_last` — the slot right
after the process it last ran — wrapping around.  When it finds a runnable
process it advances the hint past it.

*Before:* all 8 CPUs started every scan at `ptable.proc[0]`.  Under a
test with 8 runnable processes they all raced to run process 0 first, then 1,
etc.  Processes at the end of the array saw higher scheduling latency.

*After:* CPUs naturally spread across the table.  With NPROC=128 and fewer
than 128 runnable processes the expected distance between adjacent CPUs'
hints grows, reducing both scheduling latency variance and the probability
that two CPUs select the same process's slot before one can claim it.

**b) Idle `hlt`:**
```c
if(!found){
    release(&ptable.lock);
    asm volatile("hlt");   // was: release + spin in outer for(;;)
}
```
When a scheduling pass finds zero RUNNABLE processes the CPU:
1. Releases `ptable.lock`
2. Executes `hlt` — the CPU halts and draws minimal power until the next
   interrupt (timer, disk completion, network packet, etc.)

*Before:* all idle CPUs spun in a tight loop: `sti(); acquire(); [scan 64];
release()` at the timer clock rate.  On a system with one interactive user
and no background work, all 7 idle CPUs were hammering `ptable.lock` 100
times per second each.  This was the dominant source of inter-CPU lock
contention on a lightly-loaded auxiliary system.

*After:* idle CPUs do not touch `ptable.lock` at all between interrupts.
The timer interrupt on CPU 0 runs `wakeup(&ticks)` which sets any
`sleep(&ticks,...)`-waiting processes RUNNABLE and triggers a real
scheduling decision.

---

### 7. `proc_check_alarms()` Fast Path (`kernel/core/proc.c`, `kernel/core/sysproc.c`)

**Problem:** The timer ISR on CPU 0 called `proc_check_alarms()` every 10 ms.
That function acquired `ptable.lock` and scanned all 128 processes even when
no `alarm()` had ever been called.  100 lock acquires per second, 128
iterations each, purely to confirm "still nothing to do."

**Fix:** Added `static volatile int active_alarm_count` — an atomic counter
of processes that currently have a pending `alarm_ticks` deadline.

`proc_check_alarms()` now checks this counter before acquiring any lock:
```c
if(active_alarm_count == 0)
    return;   // zero lock ops, zero scan
```

`proc_set_alarm(p, deadline_ticks)` is a new helper (called by `sys_alarm`)
that sets `p->alarm_ticks` and atomically adjusts the counter using
`__sync_fetch_and_add` / `__sync_fetch_and_sub`.

On a system with no `alarm()`-using processes — the normal case — zero lock
operations occur in the timer ISR for alarm checking.

---

### 8. Merged Syscall-Return Signal Dispatch (`kernel/core/trap.c`, `kernel/core/proc.c`)

**Problem:** The syscall-return path in `trap()` contained three consecutive,
independent `acquire(ptable.lock)` + action + `release(ptable.lock)` cycles:
```c
proc_apply_pending_signals(myproc());  // acquire + scan + release
proc_deliver_signal(myproc());         // acquire + scan + release
proc_maybe_stop_current();             // acquire + check + release
```
These ran on **every** syscall and trap return to userspace, even for
processes that had no pending signals, caught signals, or stopped state.

**Fix:** A new wrapper `proc_handle_signals_on_return(p)` with a lockless
precheck:
```c
void proc_handle_signals_on_return(struct proc *p) {
    if(p == 0) return;
    if(!(p->sig_pending || p->sig_caught || p->state == STOPPED))
        return;   // common case: zero lock ops
    proc_apply_pending_signals(p);
    proc_deliver_signal(p);
    proc_maybe_stop_current();
}
```
The three fields are read without the lock.  Stale zero reads just mean
signal processing is deferred one syscall — acceptable since signals are
always delivered before returning to userspace.  For processes with no
pending work, zero lock operations occur on syscall return.

The same wrapper replaces both call sites in `trap.c` (syscall path and
trap path).

---

### 9. Buffer Cache Hash Table (`kernel/fs/bio.c`, `include/buf.h`)

**Problem:** `bget()` used two O(NBUF) linear scans under `bcache.lock`:
1. Cache-hit scan (most common path) — walks MRU→LRU checking `(dev,blockno)`.
2. Eviction scan — walks LRU→MRU looking for `refcnt==0 && !B_DIRTY`.

With the old NBUF=30 this was at most 30 comparisons.  With NBUF=128 an
unoptimised hit scan would be 128 comparisons under the global spinlock on
every block access — every `read()`, `write()`, directory traversal, etc.

**Fix:** Added a 64-entry hash table alongside the existing LRU doubly-linked
list.  The LRU list is preserved for eviction ordering.

Hash function:
```c
#define BCACHE_HASH_SIZE 64
#define BHASH(dev, blockno) \
    (((uint)(dev) * 31u + (uint)(blockno)) & (BCACHE_HASH_SIZE - 1))
```

Hit path (O(1), average ~2 comparisons):
```c
for(b = bcache.hash[h]; b; b = b->hash_next)
    if(b->dev == dev && b->blockno == blockno) ...
```

On eviction the buffer is removed from its old hash chain and inserted into
the new one.  A `B_INHASH` flag tracks whether a buffer is currently in a
chain.  New field `buf.hash_next` is added to `include/buf.h`.

---

### 10. Inode Cache Hash Table (`kernel/fs/fs.c`, `include/file.h`)

**Problem:** `iget()` scanned all NINODE entries under `icache.lock` on every
inode lookup.  This function is called on every `open()`, `stat()`, `exec()`,
`chdir()`, `link()`, `unlink()`, directory traversal step, etc.  With
NINODE=200 (raised from 50), an unoptimised scan would be 200 comparisons
under a global spinlock on every one of those operations.

**Fix:** Same hash-table pattern as the buffer cache.  64-entry table
(`icache.hash[ICACHE_HASH_SIZE]`), `struct inode` gains `hash_next`, and
`iget()` performs an O(1) hash lookup before falling through to linear scan
for the free-slot search (which only occurs on cache misses requiring slot
recycling).

Slot recycling correctly removes the old entry from its hash chain before
reassigning `dev`/`inum`.

---

## Performance Impact Summary

The most impactful individual changes, in expected order of effect:

1. **NBUF 30→128** — eliminates the near-constant buffer eviction/disk-IO
   that dominated filesystem latency.
2. **Idle `hlt` in scheduler** — eliminates inter-CPU `ptable.lock` hammering
   from idle CPUs.
3. **`kfree` poison removed** — eliminates 4 KB writes on every page free; most
   visible on `exec`-heavy workloads.
4. **Buffer cache hash table** — O(1) hit path; pays off proportionally
   to filesystem activity.
5. **Inode cache hash table** — same; most visible on workloads with many
   distinct open files.
6. **`mycpu()` O(1)** — small but pervasive; every lock operation is faster.
7. **Spinlock `pause`** — measured improvement on HT QEMU runs; prevents
   pipeline stalls during lock contention.
8. **Syscall-return signal fast path** — eliminates 3 lock acquire/release
   cycles per syscall for signal-free processes.
9. **`proc_check_alarms` fast path** — eliminates O(NPROC) scan on every
   timer tick when no alarms are set.
10. **Scheduler per-CPU offset** — reduces scheduling latency variance.

---

## Remaining Known Bottlenecks (Not Yet Fixed)

See `docs/kernel-perf-roadmap.md` for the full deferred backlog with
benefit/risk analysis.

| Bottleneck | Location | Complexity | Notes |
|------------|----------|------------|-------|
| Per-CPU `kalloc` freelists | `kalloc.c` | Medium | **Next priority** — see roadmap |
| Per-channel `wakeup` wait queues | `proc.c` | High | Replace O(NPROC) scan per wakeup |
| MLFQ / priority scheduling | `proc.c` | High | Interactive vs batch fairness |
| Per-CPU run queues | `proc.c`, `proc.h` | Very High | Full SMP scheduler restructuring |
| `ptable.lock` split | `proc.c` | High | State transitions vs signal delivery |
| `bcache.lock` striping | `bio.c` | Medium | Per-hash-bucket spinlocks |
| Framebuffer dirty-rectangle / incremental flush | `kernel/gfx/*` | Medium | **Out of scope for this kernel-core perf tranche**; track in graphics roadmap/docs |

---

## Performance Ruler and Targets (`schedperf`, `fsperf`)

Both stress binaries now report:

- Functional status (`[PASS]` / `[FAIL]`) for regressions
- Per-test throughput metrics (`[PERF] ...`)
- A weighted score normalized to `0..100`

Current target threshold for both tools:

- **Target score:** `>= 75/100`

This is intentionally a moving target rather than a binary gate.  It gives us
a stable ruler to compare runs before/after kernel changes while preserving
the existing regression checks.

`schedperf` score components include fork throughput, yield throughput, pipe
wakeup message rate, alarm/signal rate, scheduler spread throughput, and
process-table capacity headroom.

`fsperf` score components include FD ceiling, open rate, inode churn rate,
buffer-cache sequential throughput, concurrent open throughput, parallel write
throughput, inode-capacity headroom, and hash verification throughput.

---

## Debug Flags Reference

| Flag | Effect | Enabled by default |
|------|---------|--------------------|
| `-DKDEBUG_KFREE_POISON` | `kfree()` writes `0x01` to freed pages | **No** |
| `-DKDEBUG_SPINLOCK_CALLSTACK` | `acquire()` records 10-frame call stack | **No** |

Add to `CFLAGS` in `Makefile` when debugging memory/lock bugs.

---

## Test Binaries

Two dedicated stress programs exercise the changed subsystems:

| Binary | Source | Tests |
|--------|--------|-------|
| `_schedperf` | `user/schedperf.c` | Scheduler (fork storm, yield, pipe wakeup, alarm, signal, per-CPU spread) |
| `_fsperf` | `user/fsperf.c` | Buffer cache + inode cache (fd table, concurrent opens, bcache churn, inode churn, parallel writers) |

Run on a booted system:
```
$ schedperf
$ fsperf
```
Both print `[PASS]` / `[FAIL]` per sub-test and a final summary.
