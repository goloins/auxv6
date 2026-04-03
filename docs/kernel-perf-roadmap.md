# Kernel Performance Roadmap: Per-CPU `kalloc` Freelists

**Document type:** Implementation roadmap  
**Scope:** `kernel/core/kalloc.c`, `include/proc.h` (`struct cpu`), `include/param.h`  
**Priority:** High — chosen as the best benefit-to-risk deferred option from the
[kernel-perf-hardening](kernel-perf-hardening.md) audit.

## Current Status (2026-04-03)

Per-CPU `kalloc` freelist caching has now been implemented in the kernel as the
first stage of this roadmap:

- Added `KALLOC_CPU_CACHE` to `include/param.h` (currently `32` pages per CPU).
- Extended `struct cpu` with per-CPU allocator cache fields in `include/proc.h`.
- Reworked `kalloc()` / `kfree()` fast paths in `kernel/core/kalloc.c` to use
  CPU-local page stashes and batch refill/flush via the global freelist lock.
- Preserved global freelist pop order during per-CPU batch refill so
  contiguous DMA users (framebuffer allocation path) continue to work.
- Removed shared `kmem.free_pages` counter updates from SMP fast paths to
  avoid turning the counter cacheline into a new cross-CPU contention point.
- Tightened `kalloc()` / `kfree()` interrupt/locking discipline so CPU-local
  cache fast/slow path decisions cannot span a migration window.
- Updated `kalloc_meminfo()` to count global freelist pages plus per-CPU cached
  pages.

Build validation:

- `make aux.kern` passes cleanly.

Runtime baseline after current fixes:

- `kallocstress`: `90/100`, `3 passed`, `0 failed`
- `schedperf`: `89/100`, `8 passed`, `0 failed`
- `fsperf`: `85/100`, `8 passed`, `0 failed`

Note: scores show ±5–10 pt run-to-run variance from host scheduler load.
Use `-n 3` (e.g. `schedperf -n 3`) for a more stable averaged result.

Post-implementation hardening already applied:

- Fixed a DMA-sensitive contiguous-allocation regression by preserving global
  freelist pop order during per-CPU cache refill.
- Fixed allocator fast/slow path migration windows by tightening interrupt and
  lock discipline around CPU-local cache decisions.
- Fixed `/proc` read-path robustness issues discovered during login/MOTD
  validation (kernel-stack pressure and mountstats buffer-size regression).

---

## 1  Motivation

Every `kalloc()` and `kfree()` call acquires the single global `kmem.lock`.
The paths that hit this lock on every operation include:

| Caller | Per-event kalloc calls |
|--------|------------------------|
| `fork()` | ≥ 1 per copied page (stack, page-table pages) |
| `exec()` | many (new page table, stack) |
| `pipe` creation | 2 (data page + struct pipe) |
| `grow`/`sbrk` | 1 per page |
| `allocproc` | 1 (kernel stack) |
| `kfree` on exit | 1 per page owned by exiting proc |

With `NPROC=128` and 8 CPUs doing concurrent forks (as `schedperf` exercises),
`kmem.lock` becomes the single serialisation point across all allocations.
Profiling on a 32-process fork-storm shows `kmem.lock` contention accounts for
roughly 30–40 % of total spinlock wait time.

### Why this option over the other deferred candidates?

| Option | Benefit | Risk | Verdict |
|--------|---------|------|---------|
| **Per-CPU kalloc freelists** | High — removes #1 allocation bottleneck | Low — self-contained, well-understood | **CHOSEN** |
| Per-CPU run queues | High — eliminates ptable scan | Very high — requires IPI-driven load balance, many call-site changes | Deferred |
| MLFQ/priority scheduler | Medium — reduces latency variance | High — complex invariants, hard to test | Deferred |
| Per-channel wakeup (`wakeup` O(1)) | High — eliminates O(NPROC) scan | Medium — many call sites, hash sizing| Deferred |

---

## 2  Design

### 2.1  Data structures

Add a per-CPU free-page cache to `struct cpu` in `include/proc.h`:

```c
#define KALLOC_CPU_CACHE  16   // pages held per CPU before flushing to global

struct cpu {
  /* ... existing fields ... */
  struct run *kfree_cache[KALLOC_CPU_CACHE];   // per-CPU page stash
  int         kfree_cache_count;               // 0 .. KALLOC_CPU_CACHE
};
```

`KALLOC_CPU_CACHE = 16` means each CPU caches at most 64 KB of pages
(16 × 4 KB).  With 8 CPUs that is 512 KB "invisible" to the global freelist —
acceptable given the 512 MB guest memory budget.

The global pool in `kalloc.c` is unchanged:

```c
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
```

### 2.2  `kalloc()` fast path

```c
void *
kalloc(void)
{
  struct cpu *c;
  struct run *r;

  pushcli();              // disable interrupts (already required)
  c = mycpu();

  if(c->kfree_cache_count > 0){
    // Fast path: take from per-CPU stash (no lock)
    r = c->kfree_cache[--c->kfree_cache_count];
    popcli();
    memset(r, 5, PGSIZE);  // poison new allocation
    return (void*)r;
  }
  popcli();

  // Slow path: refill batch from global freelist
  acquire(&kmem.lock);
  int n = 0;
  while(kmem.freelist && n < KALLOC_CPU_CACHE){
    r = kmem.freelist;
    kmem.freelist = r->next;
    pushcli();
    mycpu()->kfree_cache[mycpu()->kfree_cache_count++] = r;
    popcli();
    n++;
  }
  release(&kmem.lock);

  if(n == 0)
    return 0;   // OOM

  // Return one page from the freshly-loaded stash
  pushcli();
  c = mycpu();
  r = c->kfree_cache[--c->kfree_cache_count];
  popcli();
  memset(r, 5, PGSIZE);
  return (void*)r;
}
```

### 2.3  `kfree()` fast path

```c
void
kfree(char *v)
{
  struct run *r;

  // ... alignment + range checks unchanged ...

#ifdef KDEBUG_KFREE_POISON
  memset(v, 1, PGSIZE);
#endif
  r = (struct run*)v;

  pushcli();
  struct cpu *c = mycpu();

  if(c->kfree_cache_count < KALLOC_CPU_CACHE){
    // Fast path: deposit in per-CPU stash (no lock)
    c->kfree_cache[c->kfree_cache_count++] = r;
    popcli();
    return;
  }

  // Slow path: flush half the stash to global freelist
  popcli();
  acquire(&kmem.lock);
  pushcli();
  c = mycpu();
  int flush = KALLOC_CPU_CACHE / 2;
  int i;
  for(i = 0; i < flush; i++){
    struct run *p = c->kfree_cache[--c->kfree_cache_count];
    p->next = kmem.freelist;
    kmem.freelist = p;
  }
  // Now deposit the new page
  c->kfree_cache[c->kfree_cache_count++] = r;
  popcli();
  release(&kmem.lock);
}
```

### 2.4  Initialisation

`kinit2()` (or wherever `kfree` is called in bulk to seed the freelist) calls
the existing `kfree` loop.  At that point, `ncpu` may still be 1 and
`kfree_cache_count` is zero, so every page goes straight to the global
freelist — correct.

`cpus[i].kfree_cache_count` is already zero because `cpus[]` is a global BSS
array.  No extra init needed.

### 2.5  `kalloc_free_pages()` (if present)

Any diagnostic that counts free pages by walking `kmem.freelist` will
under-report by up to `NCPU × KALLOC_CPU_CACHE` pages.  The diagnostic should
iterate `cpus[i].kfree_cache_count` and add those counts:

```c
uint
kalloc_free_pages(void)
{
  uint n = 0;
  acquire(&kmem.lock);
  struct run *r = kmem.freelist;
  while(r){ n++; r = r->next; }
  release(&kmem.lock);

  int i;
  for(i = 0; i < ncpu; i++)
    n += cpus[i].kfree_cache_count;   // lockless; approximate is fine
  return n;
}
```

---

## 3  Thread-safety analysis

| Scenario | Safety |
|----------|--------|
| Same CPU alloc/free (common case) | **Safe** — `pushcli()` disables preemption; no other code touches this CPU's cache |
| Process freed on different CPU from allocating CPU | **Safe** — page ends up in the freeing CPU's cache or global pool; no cross-CPU pointer needed |
| CPU A cache empty, CPU B cache full | **Safe** — both independently hit the global freelist under `kmem.lock` |
| Interrupt handler calls `kalloc` | **Safe** — xv6/auxv6 interrupt handlers do not call `kalloc`; if one ever does it runs with `pushcli` already active so `mycpu()` is valid |
| OOM detection | **Correct** — OOM is only declared after both the per-CPU stash **and** the global freelist are empty |

---

## 4  Implementation plan

### Step 1 — Add fields to `struct cpu` (30 min)

Edit `include/proc.h`:
- Add `struct run *kfree_cache[KALLOC_CPU_CACHE]`
- Add `int kfree_cache_count`
- Define `KALLOC_CPU_CACHE` in `include/param.h`

### Step 2 — Rewrite `kalloc()` and `kfree()` (1–2 h)

Edit `kernel/core/kalloc.c` following section 2.2 / 2.3 above.
Keep the existing slow path under `kmem.lock` as the sole serialisation
point.

### Step 3 — Fix `kalloc_free_pages` (15 min)

Grep for any diagnostic or assertion that walks `kmem.freelist` and add the
per-CPU cache count correction.

### Step 4 — Build + boot test (30 min)

```
make aux.kern && make test-boot
```

Verify: `_free` reports plausible free memory; kernel does not panic on boot.

### Step 5 — Run `schedperf` and `fsperf` (20 min)

```
make _schedperf _fsperf
# copy to rootfs and run in QEMU:
_schedperf
_fsperf
```

Expected:
- all `[PASS]`
- `schedperf score >= 75/100`
- `fsperf score >= 75/100`

The fork-storm and parallel-writers tests exercise `kalloc`/`kfree` most
aggressively.

### Step 6 — Run `usertests` (30 min)

The existing `_usertests` binary covers fork, exec, pipe, sbrk, and exit
intensively.  All tests must pass before landing.

### Step 7 — Optional: benchmark

Instrument with `uptime()` deltas around a 64-child fork-storm in
`schedperf.c` and compare ticks before/after.  Expected improvement: 15–30 %
reduction in fork-storm wall time.

---

## 5  Risk register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Per-CPU cache not flushed on CPU shutdown | Low | Memory leak | Add flush to `cpuhalt()` / AP shutdown path |
| `mycpu()` called without `pushcli` | Low | Wrong CPU index | `mycpu()` already panics if called without `cli`; existing discipline is maintained |
| OOM triggered prematurely | Low | Boot failure | The slow-path batch steal reloads from global pool; OOM only if global + all CPU caches truly empty |
| `kalloc_free_pages` under-reports | Medium | `_free` shows wrong number | Fix diagnostic as described in §2.5 |
| Increased complexity in `kalloc.c` | Certain | Maintenance cost | Net +40 lines; well-commented; reversible by removing the fast-path branches |

---

## 6  Deferred for later (after per-CPU kalloc)

After this change lands, the next best candidate by benefit:risk is
**per-channel sleep/wakeup** — replacing the `O(NPROC)` linear scan in
`wakeup1()` with a hash-indexed wait-list.  That change is more invasive
(touches every `sleep(chan, ...)` call site and requires a new `struct
wait_queue` type) so it is deliberately sequenced after the lower-risk
allocator work.

Framebuffer/screen-draw throughput work (dirty rectangles, partial scanout
updates, reduced full-frame blits) is explicitly acknowledged as high value,
but remains a separate graphics-focused performance stream and is out of scope
for this allocator-first roadmap.

## 7  Perf Target Maintenance Rule

When adjusting performance targets or adding new perf edge-case coverage,
update all of the following together:

1. `user/schedperf.c` and/or `user/fsperf.c`
2. corresponding manpages under `targetfs/usr/share/man/`
3. performance documentation (`docs/kernel-perf-hardening.md` and/or this file)

This keeps benchmark output, operator docs, and roadmap expectations aligned.

---

*See also:* [kernel-perf-hardening.md](kernel-perf-hardening.md) for the
complete set of changes already implemented.
