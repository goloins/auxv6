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
| Per-CPU `kalloc` freelists | `kalloc.c` | Medium | **In progress** — implementation started; see roadmap |
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

### Iteration Discipline (Required)

For every performance-target iteration, update all three in the same commit:

- `user/schedperf.c` and/or `user/fsperf.c` targets/tests/profile marker
- corresponding manpage(s) in `targetfs/usr/share/man/`
- this section (or roadmap section) with any changed threshold rationale

This prevents score drift where binaries, docs, and expected outcomes diverge.

### Current Baseline Snapshot (2026-04-04)

Latest confirmed results are now best read as a phase timeline:

- Historical best before sparse-fork experiment:
   - `schedperf -n 3`: `83/100` avg, `24 passed`, `0 failed`
   - `fsperf -n 5`: `86/100` avg (min 86, max 87), `40 passed`, `0 failed`
   - `kallocstress -n 3`: `88/100` avg, `9 passed`, `0 failed`
- Sparse-fork experiment (reverted):
   - Severe regressions (`kallocstress 88->6`, `schedperf 83->72`, `fsperf 85->78`)
- Phase 1 allocator groundwork baseline:
   - `kallocstress -n 3`: `88/100` avg
   - `schedperf -n 3`: `83/100` avg
   - `fsperf -n 3`: `86/100` avg
- Phase 2 first policy:
   - `kallocstress -n 3`: `83/100` avg
   - `schedperf -n 3`: `81/100` avg
   - `fsperf -n 3`: `84/100` avg
- Phase 2b retune outcome:
   - Build clean but caused a login-path panic in guest; rolled back.
- Current stable post-rollback checkpoint:
   - `kallocstress -n 3`: `82/100` avg (min 82, max 83)
   - `schedperf -n 3`: `81/100` avg (min 80, max 82)
   - `fsperf -n 3`: `83/100` avg (min 81, max 85)
   - `/proc/vmstat` key counters: `cache_alloc_hits 7134`, `cache_alloc_misses 0`,
     `global_refill_batches 438`, `global_drain_batches 321`
- Phase 2c validated (single-lever refill hysteresis):
    - `kallocstress -n 3`: `84/100` avg (min 83, max 85)
    - `schedperf -n 3`: `81/100` avg (min 81, max 81)
    - `fsperf -n 3`: `86/100` avg (min 86, max 86)
    - `/proc/vmstat` key counters: `cache_alloc_hits 6669`, `cache_alloc_misses 0`,
       `global_refill_batches 405`, `global_drain_batches 288`
    - Delta vs post-rollback checkpoint: `kallocstress +2`, `schedperf +0`,
       `fsperf +3`, with lower refill/drain batch traffic.
- Phase 3 tranche-1 VM scaffolding validated (no COW semantics yet):
    - `kallocstress -n 3`: `85/100` avg (min 85, max 85)
    - `schedperf -n 3`: `81/100` avg (min 81, max 82)
    - `fsperf -n 3`: `85/100` avg (min 85, max 86)
    - `/proc/vmstat` key counters remained stable: `cache_alloc_hits 6669`,
       `cache_alloc_misses 0`, `global_refill_batches 405`,
       `global_drain_batches 288`
    - Outcome: no observable regression and no stability incident after the
       dealloc teardown helper refactor.
- Phase 4 slice-1 COW fork/fault path validated:
    - `/proc/vmstat`: `ref_increments 172`, `deferred_frees 172`
    - `kallocstress -n 3`: `84/100` avg (min 84, max 84)
    - `schedperf -n 3`: `82/100` avg (min 82, max 82)
    - `fsperf -n 3`: one transient low outlier (`52/100`) amid otherwise normal
       runs; confirmation `fsperf -n 5` stabilized at `85/100` avg (min 85, max 85)
    - Outcome: promote slice-1; treat low fsperf single run as host-noise outlier.
- Phase 4 slice-2 COW scope expansion validated:
    - `/proc/vmstat`: `ref_increments 371`, `deferred_frees 371`
    - `kallocstress -n 3`: `92/100` avg (min 92, max 92)
    - `schedperf -n 3`: `87/100` avg (min 87, max 88)
    - `fsperf -n 3`: `87/100` avg (min 87, max 87)
    - Outcome: best combined score band in the current Track 0/Phase 4 cycle
       with stable behavior; keep mapping scope fixed and harden correctness next.
    - Focused correctness gate: `stackgrowtest` and `stackgrowtest -d` both pass,
       including fork-inherited growth and expected SIGSEGV overflow termination.

Scores show meaningful run-to-run variance (~5–10 pts) driven by host scheduler
load rather than code changes.  Use `schedperf -n 3` / `fsperf -n 3` /
`kallocstress -n 3` to get an averaged result before drawing conclusions (see
§ Test Binaries below).  The threshold target for all three tools is `>= 75/100`.

Known weak areas to track for future tightening:
- `fork-storm` (schedperf): ~165-184 fork/s vs 600 target
- `sched-spread` (schedperf): ~1523-1600 yield/s vs 2200 target
- `parallel-writers` throughput (fsperf): ~152-188 KB/s vs 900 target
- `inode-limit-rate` throughput (fsperf): ~136-139 open/s vs 600 target
- `pipe-page-churn` (kallocstress): ~165-172 round/s vs 320 target

Recorded experimental result (flush burst=4, reverted):
- `kallocstress`: 93/100 (+3), `schedperf`: 90/100 (+1), `fsperf`: 55/100 (−30)
- Verdict: reducing flush burst from 16→4 helps fork-heavy workloads but severely
  increases global lock acquisition frequency under sustained buffer-cache frees.
  Reverted to `KALLOC_CPU_CACHE / 2` flush.

Stability note from this iteration:
- `procfs_readi` was hardened to avoid kernel-stack pressure from large local
   metadata arrays in `/proc` read paths.
- A follow-up regression in `/proc/mountstats` generation (incorrect buffer-size
   accounting after refactor) was fixed; `cat /proc/mountstats` now reads
   correctly and login MOTD token expansion reports valid free-mem/free-disk data.

Track 0 follow-through update:
- Added `/proc/schedstat` to expose low-overhead scheduler activity counters:
   - `passes`: scheduler outer-loop iterations across all CPUs
   - `idle_halts`: number of idle `hlt` transitions across all CPUs
   - `picks`: RUNNABLE selections dispatched by the scheduler
- Counters are maintained per-CPU in `struct cpu` and aggregated locklessly for
   read-side observability, keeping runtime overhead minimal.

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
| `_kallocstress` | `user/kallocstress.c` | Allocator correctness/perf (fork+copyuvm pressure, pipe page churn, reclaim sanity) |

Run on a booted system:
```
$ schedperf
$ fsperf
$ kallocstress
```
Each prints `[PASS]` / `[FAIL]` per sub-test and a final summary.

`fsperf` now also prints the detected `/tmp` backend from `/proc/mountstats`
and emits a note when it is not `tmpfs`, because its profile targets assume
an in-memory tmpfs workload.

Follow-up fix: backend detection now parses mount-path tokens by explicit
token length (instead of relying on accidental null termination), which fixes
false `backend=unknown` reports when `/proc/mountstats` lines are space-delimited.

Validation note: post-fix guest runs now report `/tmp backend=tmpfs` as expected.

Focused fsperf follow-up (2026-04-04):
- tmpfs directory entry lookup now uses a per-directory hash index
   (`TMPFS_DHASH_SIZE=32`) in addition to the existing linked list used for
   directory iteration order.
- `tmpfs_dirent_lookup()` is now hash-chain based instead of linear scans over
   all children; `tmpfs_dirent_add()`/`tmpfs_dirent_remove()` now maintain both
   list and hash links.
- tmpfs pathname walk now traverses in-memory tmpfs nodes directly and creates
   an inode object only for the final resolved node/parent result, avoiding
   per-component `tmpfs_make_inode()`/`iput()` churn on hot open/create paths.
- Goal: reduce create/open name-lookup cost in write-heavy and open-scaling
   tests (`parallel-writers`, `inode-limit-rate`) without changing scheduler or
   allocator behavior.

Fork/VM regression and revert (2026-04-04, post-run):

Sparse `copyuvm()` (skip `!PTE_U` guard/reserve pages) was applied and
rebuilt.  Guest benchmark run revealed severe regressions:
   - kallocstress:  88/100 → 6/100  (fork-copyuvm 16/s, pipe-page-churn 15/s)
   - schedperf:     83/100 → 72/100 (fork-storm 71/s was ~390/s)
   - fsperf:        85/100 → 78/100

Root cause: with sparse fork, child processes lack pre-mapped guard/reserve
stack pages.  During child execution (libc exit path, atexit handlers, stdio
flush), deeper stack frames trigger repeated page-fault → allocuvm cycles in
the trap handler, with a full switchuvm()/TLB flush per fault.  This per-fault
allocuvm path is ~10× slower than flipping a pre-mapped PTE.

Action: reverted `copyuvm()` to dense (copy all PTE_P pages).  Dense fork
keeps all guard/reserve pages pre-allocated so stack growth stays on the fast
setpteu path.  `proc_try_grow_stack()` dual-mode is retained as a defensive
fallback; the `pst==0` branch is now dead code under normal operation.

**Next correct step for fork speedup: copy-on-write fork.**  COW eliminates
all page-copy work at fork time; children share physical pages read-only until
they write (copy-on-write fault).  That is the real path to Linux/BSD parity
here, not sparse pre-alloc skipping.

Allocator/fork redesign reset (2026-04-04, post-revert retest):

After reverting sparse fork, `kallocstress` improved from `6/100` to `31/100`,
but this is still far below target and confirms that the remaining problem is
structural rather than a single leaked-page bug or one bad branch in `copyuvm()`.

What the current numbers mean:
- `fork-copyuvm 69/s`: eager address-space duplication is still too expensive.
- `pipe-page-churn 65 round/s`: page allocation/free is still globally
   serialized enough to dominate a pipe-heavy loop.
- `allocator-reclaim 62 fork/s`: reclaim correctness is intact, but the free
   path is too expensive under repeated fork/exit churn.

Current allocator architecture limitations:
- `kernel/core/kalloc.c` still uses one global freelist protected by
   `kmem.lock`.
- The per-CPU cache is only a small fixed stash (`KALLOC_CPU_CACHE=32`), not a
   true per-CPU allocator; empty-cache refill and flush still funnel through the
   single global list.
- `fork()` in `kernel/core/proc.c` still calls dense `copyuvm()` and pays the
   full copy cost before the child even runs.

Reset plan for modernity:
1. Replace the single global free-page list with real per-CPU page magazines or
    per-CPU free lists, using batched steal/rebalance only when a CPU-local pool
    crosses low/high watermarks.
2. Add physical-page reference counts so page-table entries can safely share
    backing pages across address spaces.
3. Convert `fork()` from eager `copyuvm()` to copy-on-write mappings plus a
    write-fault slow path.
4. Rework child wait/reap bookkeeping around per-parent child lists so fork
    storms stop paying repeated full `ptable.proc[]` scans.
5. Re-benchmark only after those structural changes land; do not spend more
    time on micro-tuning cache flush batch sizes or sparse pre-allocation tricks.

Production-kernel allocator/VM refactor map (2026-04-04):

Primary durable reference: see `docs/allocator-vm-refactor-blueprint.md` for the
complete phase plan, invariants, risk model, validation matrix, and file-by-file
impact map.  This section remains the condensed summary; the blueprint document
is the anti-context-collapse source of truth.

Phase 1 landing status (2026-04-04):
- landed in-tree
- kernel build clean
- guest perf validation passed:
   - `kallocstress -n 3`: `88/100` avg (min 88, max 89)
   - `schedperf -n 3`: `83/100` avg
   - `fsperf -n 3`: `86/100` avg (min 86, max 87)
- scope landed:
   - PFN-indexed page metadata in `kernel/core/kalloc.c`
   - managed-page refcount groundwork (`kfree()` only returns pages to free lists
      at refcount zero)
   - allocator counters for alloc/free, per-CPU cache hits/misses, and batch
      refill/drain activity
   - `kalloc_stats()` helper plus page-refcount helper API
   - new `/proc/vmstat`
   - expanded `/proc/meminfo` with page-count lines
- scope not yet landed:
   - copy-on-write fork
   - lazy heap allocation
   - typed object allocator
   - child-list wait/reap conversion

Phase 1 opinion:
- This tranche is a success and is worth keeping exactly as the new baseline.
- It restored `kallocstress` from the degraded post-revert state back to the
   historical best band without harming `schedperf` or `fsperf`.
- The counters are already informative and internally consistent.

Observed first `/proc/vmstat` sample interpretation:
- `pages_shared 0`, `ref_increments 0`, and `deferred_frees 0` are expected in a
   pre-COW, pre-shared-page kernel.
- `cache_alloc_hits 6449` vs `cache_alloc_misses 201` indicates the common
   single-page path is already mostly local after boot.
- `free_calls` greatly exceeding `alloc_calls` is expected because the counter
   includes initial `freerange()` population and subsequent release traffic.
- The important outcome is that the counters look sane and the benchmark suite
   returned to the expected score band.

Phase 2 launch status (2026-04-04):
- landed in-tree, build clean, guest validation complete with regression
- scope landed:
   - explicit per-CPU allocator policy knobs in `include/param.h`
      (`KALLOC_PCPU_LOW_WATER`, `KALLOC_PCPU_HIGH_WATER`,
      `KALLOC_REFILL_BATCH`, `KALLOC_DRAIN_BATCH`, `KALLOC_GLOBAL_RESERVE`)
   - explicit local refill/drain helpers in `kernel/core/kalloc.c`
   - batched local<->global page movement on watermark thresholds
   - external allocator API unchanged (`kalloc`, `kfree`)
- expected immediate observable effects in `/proc/vmstat` after load:
   - lower `cache_alloc_misses` ratio
   - fewer but larger `global_refill_pages`/`global_drain_pages` movements
   - stable `pages_free` accounting under stress loops

First Phase 2 validation results (2026-04-04):
- `/proc/vmstat` sample:
   - `cache_alloc_hits 6666`
   - `cache_alloc_misses 0`
   - `global_refill_batches 411`
   - `global_refill_pages 6576`
   - `global_drain_batches 294`
   - `global_drain_pages 4704`
- Benchmarks vs Phase 1 baseline:
   - `kallocstress -n 3`: `83/100` (was `88/100`)
   - `schedperf -n 3`: `81/100` (was `83/100`)
   - `fsperf -n 3`: `84/100` (was `86/100`)

Interpretation:
- The current refill policy appears too aggressive: it avoids misses almost
   entirely (`cache_alloc_misses=0`) but likely buys that by taking the global
   allocator lock too often while local cache still has enough pages.
- Result: all tests still pass thresholds, but with a measurable across-the-board
   throughput drop.

Action before Phase 3:
- Retune Phase 2 low/high water and refill trigger policy; keep the architecture
   and observability, but reduce eager refill lock traffic.

Phase 2b retune (2026-04-04) now in-tree:
- build clean
- guest validation pending
- exact retune applied:
   - demand-driven refill (only when local cache depletes)
   - reduced `KALLOC_REFILL_BATCH` and `KALLOC_DRAIN_BATCH`
   - reduced `KALLOC_PCPU_LOW_WATER`
   - adjusted `KALLOC_PCPU_HIGH_WATER`

This section summarizes the public design patterns used by modern production
Unix kernels and translates them into a concrete auxv6 refactor map.  The goal
is not to clone Linux, FreeBSD, NetBSD, or OpenBSD internals verbatim; it is to
adopt the architectural common denominators they all converged on.

Public reference points reviewed for this reset:
- Linux MM docs: physical memory, page tables, page-table helper semantics.
- FreeBSD VM handbook and UMA(9): vm_page-backed VM plus slab/zone allocator.
- NetBSD UVM(9): page database, fault path, pagedaemon, copy-on-write aware VM.

Common production patterns across those systems:

1. **A real page database exists.**
    Each physical page has metadata (`struct page`, `vm_page_t`, etc.) tracking
    refcount, state, queue membership, and allocator/fault bookkeeping.

2. **The page allocator fast path is per-CPU; the global allocator is batched.**
    Frequent single-page alloc/free operations hit per-CPU caches or page lists.
    Interaction with the shared allocator happens in refill/drain batches, not on
    every allocation.

3. **Kernel object allocation is not the same thing as page allocation.**
    Production kernels separate raw page management from typed object allocators
    (slab/SLUB/SLOB/UMA pools/zones).  Small kernel objects do not each consume a
    full physical page.

4. **`fork()` does not eagerly copy all user pages.**
    It installs shared read-only mappings and relies on a copy-on-write fault path
    to allocate a private page only when a writer actually modifies data.

5. **Page faults are first-class VM operations.**
    Fault handling distinguishes demand-zero, copy-on-write, protection faults,
    and invalid accesses rather than treating all faults as hard failures with a
    tiny stack-growth exception.

6. **Memory pressure has reserves, watermarks, and reclaim policy.**
    Modern kernels preserve a minimum free-page reserve for critical paths and use
    reclaim or trimming mechanisms before the system falls into allocator collapse.

7. **Observability is part of the subsystem contract.**
    They export counters for faults, COW events, page allocations, reclaim, cache
    occupancy, and allocator misses because tuning without that visibility is
    guesswork.

What this means for auxv6 specifically:

Current architecture:
- `kernel/core/kalloc.c` is one global free-page list plus a small per-CPU page
   stash (`KALLOC_CPU_CACHE=32`).
- `kernel/core/vm.c` treats physical pages as anonymous raw memory with no page
   metadata beyond page-table presence bits.
- `kernel/core/proc.c` forks by dense `copyuvm()`, so cost scales with mapped
   address space size before the child even runs.
- `kernel/core/pipe.c` allocates one full 4 KiB page for each 512-byte pipe
   buffer object because there is no typed object allocator.
- `proc_waitpid()` still rescans `ptable.proc[]` for child discovery and reap.

Target architecture for auxv6:

Layer 1: physical page allocator
- Introduce a physical-page metadata array indexed by PFN.
- Track at least: refcount, flags, owning queue/cache state, and optional zeroed
   state.
- Replace the single freelist with per-CPU page caches backed by a global page
   allocator that moves pages in batches.
- Preserve a small reserve for kernel-critical contexts.

Layer 2: VM fault and mapping model
- Add PTE helper wrappers for present/write/user/COW transitions instead of open-
   coding raw bit twiddling in many call sites.
- Convert `fork()` to write-protect shared user mappings and increment page
   refcounts instead of allocating/copying eagerly.
- Extend the trap page-fault path to distinguish:
   - stack growth
   - demand-zero / lazy allocation
   - copy-on-write write faults
   - invalid protection faults
- Invalidate TLBs only for modified mappings/pages, not whole-address-space style
   operations wherever avoidable.

Layer 3: typed kernel object allocator
- Add a small slab/zone allocator for frequently allocated fixed-size kernel
   objects.
- First candidates: `struct pipe`, `struct file`, small VFS helper objects,
   networking control objects, and eventually buffer/inode adjunct structures.
- Back the slabs from the page allocator instead of calling `kalloc()` for each
   object instance.

Layer 4: process/VM bookkeeping
- Add per-parent child lists to `struct proc` so `wait()/waitpid()` stop doing
   repeated full table scans.
- Separate address-space lifetime from process-table slot lifetime more cleanly:
   a process should own a VM object/context rather than just a raw `pgdir` and
   scalar `sz`.

Layer 5: reclaim and observability
- Add allocator counters for per-CPU hit/miss, global refill, global drain,
   reserve usage, and allocation failure.
- Add VM counters for page faults by class, COW copies, zero-fill faults, and
   TLB-invalidating mapping updates.
- Export these through procfs before aggressive tuning.

Required codebase changes by area:

- `include/proc.h`
   - add child-list linkage and eventually a VM context pointer/structure
- `include/defs.h`
   - add page allocator, page metadata, and COW helper interfaces
- `include/mmu.h`
   - add explicit software-defined PTE bit usage/helpers for COW/ref tracking
- `include/param.h`
   - replace fixed tiny cache assumptions with tunables/watermarks for page caches
- `kernel/core/kalloc.c`
   - split into page allocator core plus per-CPU cache management; stop using raw
      freelist nodes as the only metadata
- `kernel/core/vm.c`
   - add page refcount operations, COW mapping install, COW fault resolution,
      zero-page/lazy-allocation support, and tighter TLB invalidation helpers
- `kernel/core/proc.c`
   - rework `fork()`, `exit()`, `wait()/waitpid()`, child bookkeeping, and VM
      lifetime transitions
- `kernel/core/trap.c`
   - page fault dispatcher must become VM-aware instead of stack-growth-special-case
- `kernel/core/exec.c`
   - build initial address-space objects compatible with lazy/COW semantics
- `kernel/core/sysproc.c`
   - `sbrk()` / `growproc()` should evolve toward lazy heap growth rather than
      eager physical allocation on every expansion
- `kernel/core/pipe.c`
   - migrate pipe object allocation to a typed cache/zone allocator
- `kernel/fs/*`, `kernel/net/*`
   - audit for kernel objects currently over-allocating full pages and move those
      to typed caches where appropriate
- `kernel/fs/procfs.c`
   - expose allocator/VM counters so regressions are diagnosable

Refactor sequence to minimize regression risk:

Phase 1: page metadata without behavior change
- Add a PFN-indexed page metadata table.
- Convert `kalloc()` / `kfree()` to maintain refcount/flags, but keep current API.
- Add procfs counters for allocations/frees/per-CPU cache hits/misses.

Phase 2: real per-CPU page allocator
- Replace the ad hoc stash with batched per-CPU page lists/magazines.
- Keep single-page allocation API stable for callers.
- Re-benchmark `kallocstress` before any VM semantic changes.

Phase 3: VM COW groundwork
- Introduce PTE helpers and page refcount helpers.
- Teach `freevm()` / `deallocuvm()` to drop refs instead of blindly freeing pages.
- Make no user-visible semantic changes yet beyond correctness scaffolding.

Phase 4: copy-on-write fork
- Switch `fork()` from dense `copyuvm()` to shared read-only mappings plus COW
   fault handling.
- Validate with `schedperf`, `kallocstress`, `stackgrowtest`, and userland smoke.

Phase 5: child-list wait/reap and lazy heap
- Remove repeated full process-table scans from parent/child bookkeeping.
- Consider lazy heap population for `sbrk()` only after COW fork is stable.

Phase 6: typed kernel object caches
- Move `pipe`, then other hot fixed-size objects, off raw page allocation.
- This should directly improve `pipe-page-churn` and reduce allocator pressure.

Non-goals for the first tranche:
- NUMA support, huge pages, memory compaction, swap, or full BSD-style VM object
   shadow chains.
- Replacing the direct-map kernel address model.
- Tuning secondary heuristics before primary architecture changes land.

Success criteria for the refactor:
- `kallocstress` returns to and then exceeds the historical best without special-
   case benchmark hacks.
- `schedperf` fork-storm and `fsperf` process-heavy subtests rise together rather
   than trading regressions.
- `pipe-page-churn` improves materially once pipe objects stop consuming a full
   page each.
- New counters make allocator and VM regressions attributable within one guest
   run, not after archaeology.

All three support `-n <runs>` to repeat and average over multiple runs, which
helps account for host-load variance:
```
$ schedperf -n 3
$ fsperf -n 3
$ kallocstress -n 3
```
The multi-run summary reports `avg`, `min`, and `max` scores across all runs.
Maximum supported runs: 32.
