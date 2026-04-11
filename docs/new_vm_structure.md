# auxv6-VM-NG: Modern Virtual Memory Redesign

**Date**: April 10, 2026  
**Status**: Architecture design phase (pre-implementation)  
**Target**: Land complete redesign in 12-14 weeks (10 phases)  
**Goal**: Production-grade VM system with COW, fault-driven allocation, typed caches, and observability

---

## Executive Summary

The current auxv6 VM allocator and address-space management system is reaching scalability limits:

- Single global `kmem.lock` remains the bottleneck despite per-CPU caches
- Fork still performs eager page copying at `copyuvm()` time
- Small objects waste full pages (`struct pipe` = 1 page)
- Page faults are exceptions, not a normal VM operation
- Wait/reap require O(NPROC) process table scans
- No systematic reclaim or swap support

This document specifies a clean redesign synthesizing proven patterns from **Linux**, **FreeBSD**, **OpenBSD**, and **Darwin** into a cohesive system that retains auxv6's simplicity while achieving modern scalability.

### Key Outcomes (Phase 10 complete)

- **Throughput**: 50-100% improvement on page-heavy workloads
- **Allocator**: Buddy allocator + per-CPU fast paths, O(log n) worst-case
- **Fork overhead**: Drops 50-70% (COW + lazy allocation)
- **Memory density**: 3-5x improvement for small objects (UMA-style caches)
- **Observability**: 40+ /proc/vmstat counters; per-zone, per-fault-type tracking
- **Swap support**: Optional (CONFIG_SWAP); page reclaim daemon (kswapd equivalent)

---

## Current State vs. Modern Kernels

### Current auxv6 Limitations

| Problem | Current | Modern |
|---------|---------|--------|
| Page allocation | Single global freelist + small per-CPU stash | Buddy allocator + per-CPU pages |
| Page metadata | Minimal (refcount only) | Rich (flags, queue, LRU, owner) |
| Fork semantics | Eager copy (copyuvm) | COW + lazy + shared mappings |
| Page faults | Exceptions (stack growth only) | First-class fault dispatcher |
| Small objects | One page each (kalloc) | Type-safe caches (UMA/SLAB) |
| Wait/Reap | O(NPROC) table scans | Child-list local iteration |
| Reclaim | None | Page eviction daemon (kswapd) |
| Swap | None | Full swap support with demand paging |

### Design Comparison Matrix

| Aspect | Linux | FreeBSD | OpenBSD | Darwin | **auxv6-NG Target** |
|--------|-------|---------|---------|--------|---------------------|
| Page metadata | Rich | Rich | Simple | Object-based | Rich (layer-0) |
| Allocator | Buddy+SLAB | UMA | Zone | Pager objects | Buddy+UMA (L1+L4) |
| COW strategy | Refcount | Shadow objects | pv-entries | Chains | Refcount (L2+L3) |
| Fault model | Comprehensive | Object-based | Conservative | Pager-based | Comprehensive (L3) |
| Per-CPU caches | Yes (pagesets) | Per-type | No (reserves) | No | Yes (magazines, L1) |
| Reclaim | Active (kswapd) | Laundry daemon | Conservative | Page out | Optional (phase 9) |
| Observability | Extensive | Moderate | Good | Moderate | **Comprehensive (phase 10)** |

---

## Design Principles

These must remain true throughout implementation:

1. **Separate page allocation from object allocation** — physical pages and kernel structures are different problem domains
2. **Make per-page metadata explicit** — refcounting, sharing, and reclaim require per-page state outside page tables
3. **Optimize the common SMP fast path locally** — single-page alloc/free should usually complete on local CPU without global lock
4. **Use faults to defer expensive work** — allocation and initialization happen at fault time, not fork time
5. **Make state transitions observable** — allocator changes must be countable and discoverable via /proc/vmstat
6. **Preserve correctness invariants** — no page can be free while mapped; refcount cannot underflow
7. **Design for eventual swap/reclaim** — architecture must support optional swap layer without core VM redesign

---

## Proposed Architecture: 6 Layers

### Layer 0: Physical Page Database

**File**: `kernel/vm/page.h`, `kernel/vm/pagedb.c`

Central page descriptor array indexed by PFN. Every managed page has metadata for refcounting, allocation class, queue membership, and virtual-mapping tracking.

```c
struct page_descriptor {
  // Identity (immutable)
  uint32_t pfn;
  
  // Refcount & flags
  uint16_t refcount;               // # address spaces mapping this page
  uint16_t ref_flags;              // PAGE_PINNED, PAGE_RESERVED, PAGE_ZERO, PAGE_MANAGED
  
  // Allocation context
  uint8_t alloc_class;             // USER, KERNEL, DMA, CACHE, SWAP
  uint8_t allocator_id;            // which subsystem owns it
  
  // Queue/LRU
  uint16_t queue_id;               // active, inactive, free, cache
  struct page_descriptor *lru_prev, *lru_next;
  
  // Virtual mapping tracking (for reclaim/revocation)
  struct pv_entry *pv_list;        // all VA→PA mappings for this page
  
  // Fault statistics
  uint32_t fault_count;            // demand-zero, COW, swapins
  uint16_t flags;                  // PG_LOCKED, PG_DIRTY, PG_WRITEBACK, PG_REFERENCED
  
  // Owner
  union {
    struct vfs_inode *inode;       // file-backed
    struct address_range *vm_object;  // memory object
    void *owner_hint;              // debug provenance
  } owner;
  
  // Debug (CONFIG_VM_DEBUG only)
  void *alloc_pc;
  uint64_t alloc_ts_ns;
};

// Global page array
extern struct page_descriptor *pg_array;
extern uint pg_count;

struct page_descriptor *pgfn_descriptor(uint pfn);
int pgfn_refcount(uint pfn);
void pgfn_incref(uint pfn);
void pgfn_decref(uint pfn);
```

#### Phase 0 Sizing Worksheet (auxv6 default memory map)

Use this worksheet to size `pg_array` before Phase 1 implementation:

- `PHYSTOP = 0x20000000` (512 MiB)
- `PGSIZE = 4096`
- PFN/page count:
  - $N_{pages} = \frac{PHYSTOP}{PGSIZE} = \frac{536{,}870{,}912}{4096} = 131{,}072$

Estimated metadata RAM overhead (illustrative):

- If `sizeof(struct page_descriptor) = 32` bytes:
  - $131{,}072 \times 32 = 4{,}194{,}304$ bytes (4.0 MiB)
- If `sizeof(struct page_descriptor) = 48` bytes:
  - $131{,}072 \times 48 = 6{,}291{,}456$ bytes (6.0 MiB)
- If `sizeof(struct page_descriptor) = 64` bytes:
  - $131{,}072 \times 64 = 8{,}388{,}608$ bytes (8.0 MiB)

Phase 1 target budget:

- Keep metadata overhead under ~2% of RAM when practical
- At 512 MiB physical RAM, 2% is ~10.24 MiB, so 64-byte descriptors are acceptable

Placement guidance:

- Allocate `pg_array` once during early VM init (before exposing allocator APIs)
- Mark descriptor backing pages as reserved/non-managed
- Keep PFN 1:1 indexing (`pg_array[pfn]`) to avoid hash/lookup overhead

**Exit Criteria**: All PFNs have descriptors; refcount get/put work; no semantic changes yet.

---

### Layer 1: Physical Allocator (Buddy + Per-CPU Caches)

**Files**: `kernel/vm/buddy.c`, `kernel/vm/pagealloc.c`

Three-tier page allocation:
- Per-CPU local caches (magazine-based batching)
- Per-zone buddy allocator (free lists by order: 2^0 to 2^MAX)
- Emergency reserves for critical kernel allocations

```c
// Per-CPU freelist magazines
struct per_cpu_page_cache {
  struct page_descriptor *local_free[MAX_ORDER];  // orders 0-7
  uint local_count[MAX_ORDER];
  uint hits, misses;
  struct spinlock lock;
};

// Per-zone allocator
struct zone_allocator {
  struct page_descriptor *free_lists[MAX_ORDER];  // buddy tree
  uint watermark_min, watermark_low, watermark_high;
  uint reserve_pool;               // emergency pool
  struct spinlock lock;
  
  // LRU for reclaim
  struct page_descriptor *lru_active, *lru_inactive;
  
  // Counters
  uint alloc_count, free_count;
  uint alloc_order[MAX_ORDER], free_order[MAX_ORDER];
};

struct page_descriptor *alloc_pages_order(int order, int flags);
void free_pages_order(struct page_descriptor *pg, int order);
struct page_descriptor *alloc_single_page(int flags);
void free_single_page(struct page_descriptor *pg);

// Reserve pool operations
int alloc_reserve(int flags);
void free_reserve(int flags);
int check_reserves(void);
```

**Exit Criteria**: 
- Buddy allocator passes invariant tests (no list corruption)
- Benchmarks show ≥95% performance vs. current system
- Per-CPU caches show >90% hit rates on single-page allocations

---

### Layer 2: Memory Mapping / Address-Space Management

**Files**: `kernel/vm/vma.c`, `kernel/include/vma.h`

Virtual address space explicitly modeled as a collection of VMAs (Virtual Memory Areas) with COW tracking.

```c
struct vaddr_range {
  uint va_start, va_end;
  struct page_descriptor *pages;   // allocated pages (NULL if lazy)
  uint flags;                      // VMA_READ, VMA_WRITE, VMA_EXEC, VMA_SHARED, VMA_COW
  
  // File-backing (if any)
  struct vfs_inode *inode;
  uint64_t file_offset;
  
  // NUMA node after phase 8 (reserved)
  uint node_id;
};

struct address_space {
  // Page table & VMAs
  pde_t *pgdir;
  struct vaddr_range *vmas;        // sorted array (or RB-tree later)
  uint vma_count;
  
  // COW tracking
  uint refcount;                   // # processes sharing this space
  struct address_space *parent;    // COW parent
  struct list_head children;       // COW children
  
  // Accounting
  uint vm_size;                    // total mapped VA
  uint rss;                        // resident set
  uint swap_usage;
  
  // Fault statistics
  uint faults_total, faults_cow, faults_demand_zero, faults_swapins;
  
  struct spinlock lock;
};

// VMA operations
int vma_expand(struct address_space *, uint new_size);
int vma_shrink(struct address_space *, uint new_size);
struct vaddr_range *vma_find(struct address_space *, uint va);
int vma_insert(struct address_space *, struct vaddr_range *);
void vma_remove(struct address_space *, struct vaddr_range *);

// Address space lifecycle
struct address_space *address_space_create(void);
struct address_space *address_space_dup_cow(struct address_space *);  // for fork
void address_space_release(struct address_space *);
void address_space_destroy(struct address_space *);
```

**Exit Criteria**:
- Processes have explicit address_space objects
- Fork creates COW address_space with shared parent
- VMAs track lazy pages (NULL) and populated pages
- No regression vs. layer 1+0

---

### Layer 3: Fault-Driven Allocation & COW Resolution

**Files**: `kernel/vm/fault.c`, `kernel/core/trap.c` (refactored)

First-class fault handling for demand paging, COW, and protection repair.

```c
enum fault_type {
  FAULT_INVALID_ADDR,              // segmentation violation
  FAULT_DEMAND_ZERO,               // lazy page; allocate + zero
  FAULT_READ_PROTECTION,           // not readable
  FAULT_WRITE_PROTECTION,          // not writable; may be COW
  FAULT_COW,                       // write to COW page
  FAULT_SWAP_IN,                   // page evicted to swap; reload
  FAULT_FILE_BACKED,               // read from backing file
};

struct fault_context {
  struct address_space *addrsp;
  uint vaddr;
  enum fault_type type;
  uint flags;                      // FAULT_WRITE, FAULT_USER, FAULT_EXE
  int err;                         // result
};

// Main fault entry point
int vm_handle_fault(struct fault_context *ctx);

// Subfault handlers
int fault_demand_zero(struct fault_context *);
int fault_cow_resolve(struct fault_context *);
int fault_swapout_reload(struct fault_context *);
int fault_file_read(struct fault_context *);
int fault_stack_growth(struct fault_context *);

// Helpers
int is_pte_cow(pte_t);
int is_pte_present(pte_t);
int pte_flags(pte_t);
void set_pte_cow(pte_t *);
void set_pte_writable(pte_t *);
```

**Exit Criteria**:
- Fault dispatcher correctly routes all fault types
- COW faults allocate private pages; parent refcount verified
- Demand-zero pages allocate on first write
- `stackgrowtest` passes; no regression

---

### Layer 4: Typed Object Allocator (UMA-style)

**Files**: `kernel/vm/kmem_cache.c`, `kernel/include/kmem_cache.h`

Per-type slab caches backed by Layer 1 pages. Stops wasting whole pages for small structures.

```c
struct kmem_cache {
  const char *name;
  uint obj_size;
  uint objs_per_page;              // obj_size fit count
  
  // Slab management
  struct page_descriptor *pages;   // list of cache pages
  void *freelist;                  // within-page free list
  uint objs_allocated, objs_freed;
  
  // Per-CPU magazines
  struct per_cpu_mag *pcpu_mags;
  
  struct spinlock lock;
};

struct per_cpu_mag {
  void *objs[MAG_SIZE];            // local batch
  uint count;
};

struct kmem_cache *kmem_cache_create(const char *name, uint size);
void *kmem_cache_alloc(struct kmem_cache *, int flags);
void kmem_cache_free(struct kmem_cache *, void *obj);
void kmem_cache_destroy(struct kmem_cache *);

// Pre-created caches
extern struct kmem_cache *pipe_cache;
extern struct kmem_cache *file_cache;
extern struct kmem_cache *proc_cache;
extern struct kmem_cache *inode_cache;
extern struct kmem_cache *buffer_cache;
```

**Exit Criteria**:
- pipe_cache reduces memory per pipe from 1 page to ~32 bytes
- `pipe-page-churn` benchmark improves >30%
- No regression in pipe functionality

---

### Layer 5: Process-Local Child Tracking

**Files**: `kernel/core/proc.c` (refactored)

Maintain explicit parent-child relationships so wait/reap become O(children) instead of O(NPROC).

```c
struct proc {
  // ... existing fields ...
  
  // VM context
  struct address_space *addrsp;
  
  // Child relationships
  struct proc *parent;
  struct list_head children;       // my children
  struct list_head sibling;        // my entry in parent's child list
  
  // Wait tracking
  int exit_status;
  int exit_signal;
};

// Child list operations (private to proc.c)
static void link_child(struct proc *parent, struct proc *child);
static void unlink_child(struct proc *child);
static void reparent_children(struct proc *old_parent, struct proc *new_parent);

// Modified wait/waitpid
int wait(void);
int waitpid(int pid);
int waitpid_nohang(int pid);
```

**Exit Criteria**:
- wait/waitpid iterate children, not ptable
- Reparenting is correct; zombie chains unbroken
- `schedperf` fork-storm tests improve >20%

---

## File-by-File Refactor Scope

### New Files (VM subsystem)

```
kernel/vm/
├── page.h                 # Layer 0: page descriptor interface
├── pagedb.c               # Layer 0: page descriptor database init
├── buddy.c                # Layer 1: buddy allocator core
├── pagealloc.c            # Layer 1: public alloc/free API
├── vma.c                  # Layer 2: VMA management
├── vma.h                  # Layer 2: public interface
├── fault.c                # Layer 3: fault dispatcher + handlers
├── kmem_cache.c           # Layer 4: typed object allocator
├── kmem_cache.h           # Layer 4: public interface
├── stats.c                # Observability: /proc/vmstat counters
├── reclaim.c              # Phase 9: page eviction daemon (optional)
└── swap.c                 # Phase 9: swap device interface (optional)
```

### Modified Core Files

| File | Changes | Scale |
|------|---------|-------|
| `kernel/core/vm.c` | Rewrite `setupkvm()`, `allocuvm()`, `deallocuvm()`, `copyuvm()` to use address_space model | ~1500→2500 lines |
| `kernel/core/trap.c` | Route page faults through `vm_handle_fault()`; remove stack-growth special case | ~200 lines |
| `kernel/core/proc.c` | Refactor fork/exit/wait around child lists; address_space initialization | ~500 lines |
| `kernel/core/exec.c` | Use address_space; remove eager page allocation | ~100 lines |
| `kernel/core/main.c` | Initialize page descriptor database; buddy allocator; zones | ~100 lines |
| `kernel/core/kalloc.c` | Keep as compatibility shim calling Layer 1; deprecated long-term | ~50 lines |
| `include/defs.h` | Update function signatures; add new prototypes | ~50 lines |
| `include/proc.h` | Add address_space, child-list fields to struct proc | ~20 lines |
| `include/mmu.h` | Define page table flag discipline (PTE_COW, etc.) | ~30 lines |

### Core Decomposition Plan (xv6 holdover cleanup)

Before semantic VM changes, split monolithic core files into smaller units with
strictly no behavioral changes. This mirrors Linux/FreeBSD organization and
reduces merge risk during VM bring-up.

#### Trap and IRQ split

| New/Existing File | Responsibility |
|-------------------|----------------|
| `kernel/core/trap.c` | Thin trap entry glue + IDT setup + top-level dispatch |
| `kernel/core/irq.c` | Dynamic IRQ registry (`irq_register`, `irq_unregister`, dispatch chain) |
| `kernel/core/trap_fault.c` | Fault classification and VM handoff (`vm_handle_fault`) |
| `kernel/core/trap_diag.c` | Emergency fatal trap diagnostics/reporting |

#### Proc split

| New/Existing File | Responsibility |
|-------------------|----------------|
| `kernel/core/proc.c` | Thin compatibility front + shared globals only |
| `kernel/core/proc_lifecycle.c` | `allocproc`, `fork`, `exit`, `wait`, `waitpid`, reparenting |
| `kernel/core/proc_sched.c` | scheduler core, `sleep`/`wakeup`, run-state transitions |
| `kernel/core/proc_signal.c` | process signal delivery/stop/continue mechanics |
| `kernel/core/proc_stats.c` | loadavg, wake/wait scan counters, scheduler stats APIs |
| `kernel/core/proc_fdscan.c` | fd-table/process scan helpers (device-open checks, PTY checks) |

#### Constraints for decomposition commits

- No semantic or locking changes in split commits
- No signature changes unless required for static visibility
- Build after each step using host-side `sudo make aux.kern`
- Keep each split commit bisectable and revertable

### Filesystem Layer Updates

| File | Change | Reason |
|------|--------|--------|
| `kernel/fs/inode.c` | Create inode_cache via kmem_cache | Free per-inode page waste |
| `kernel/fs/file.c` | Create file_cache | Free per-file-descriptor page waste |
| `kernel/fs/procfs.c` | Export /proc/vmstat, /proc/zoneinfo, /proc/slabinfo | Observability |
| `kernel/fs/ext2fs.c` | Buffer cache integration with page allocator | Coordinated reclaim |

### Device Driver Updates

| File | Change |
|------|--------|
| `kernel/driver/dma.c` | Zone-aware allocation (DMA32); use Layer 1 API |
| `kernel/driver/virtio.c` | Use new contiguous pages API |
| `kernel/driver/vmxnet3.c` | Use new contiguous pages API |

---

## Implementation Roadmap: 10 Phases

Each phase is self-contained, testable, and can be reverted independently. Estimated timeline: **12-14 weeks serial** (can parallelize some later phases).

### Phase 0: Measurement & Safety Planning (1 week)

**Goal**: Lock in baselines; document invariants; plan rollback strategy

**Tasks**:

**Phase 0A Progress (2026-04-10)**:
  - includes device-open/PTY scans and CWD device/reference helpers

**Validation**:

**Risk**: Low (measurement only)

---

### Phase 1: Page Descriptor Database (1 week)

- [ ] Allocate `pg_array` at boot; initialize all PFNs to valid descriptors
- [ ] Add PAGE_MANAGED, PAGE_PINNED, PAGE_RESERVED classification
- [ ] Implement refcount get/put/incref/decref helpers
- [ ] Add `pgfn_descriptor()` and basic query APIs

**Phase 1 Progress (2026-04-10)**:
- [x] Added pagedb API surface in `kernel/core/pagedb.c` and wired it into build
- [x] Added PFN query/incref/decref/is-managed wrappers (`pgfn_*`) backed by current `kpage_*` paths
- [x] Added exported symbols/prototypes (`pg_array`, `pg_count`, `pgfn_*`) in `include/defs.h`
- [x] Replaced transitional shim with boot-time runtime-backed descriptor allocation (`pagedb_init` in `main` after `kinit2`)
- [x] Added pagedb observability in `/proc/meminfo` and `/proc/vmstat` (ready/pages/bytes/backing-pages)
- [x] Added PAGE_MANAGED/PAGE_PINNED/PAGE_RESERVED/PAGE_FREE descriptor flags and flag-count observability
- [x] Plumbed managed/free state transitions from `kalloc`/`kfree` into pagedb helpers
- [x] Migrated runtime refcount authority to pagedb descriptors when pagedb is ready
- [x] Removed runtime refcount mirror writes in allocator fast/slow paths (pagedb authoritative post-init)
- [ ] Remove legacy `kpage_meta.refcount` dependency entirely (still mirrored for compatibility/transition)

- `kernel/vm/pagedb.c` (NEW)
- `kernel/core/main.c` (add pagedb init)
- `kernel/core/kalloc.c` (call pgfn_decref on free)
- `kernel/fs/procfs.c` (export new counters)

- [ ] Guest boots
- [ ] /proc/meminfo shows sensible totals
- [ ] Benchmarks show ≥99% equivalence to baseline
---

### Phase 2: Buddy Allocator Scaffold (1-2 weeks)

**Goal**: Implement buddy allocator; keep external `kalloc()` API working; no per-CPU fast path yet

**Tasks**:
- [ ] Implement `alloc_pages_order(order)` using buddy algorithm
- [ ] Implement free-list merging and splitting
- [ ] Create `alloc_single_page()` and `free_single_page()` wrappers
- [ ] Have `kalloc()` call `alloc_single_page()` internally
- [ ] Add invariant checks: free-list sanity, no double-free, refcount bounds
- [ ] Export buddy state to /proc/vmstat (alloc_order_0...7, free_order_0...7)

**Files Modified**:
- `kernel/vm/buddy.c` (NEW)
- `kernel/vm/pagealloc.c` (NEW)
- `kernel/core/kalloc.c` (refactor to call alloc_single_page)
- `kernel/fs/procfs.c` (add buddy counters)

**Validation**:
- [ ] Kernel builds
- [ ] `kallocstress -n 3` shows ≥95% performance vs. baseline
- [ ] `stackgrowtest` passes
- [ ] Free-page count stable (no leak detection)

**Risk**: Medium (buddy logic must be correct, but changes are isolated to layer 1)

---

### Phase 3: Per-CPU Page Caches (1 week)

**Goal**: Add per-CPU fast paths; batched refill/drain; reduce global lock contention

**Tasks**:
- [ ] Allocate per-CPU cache structure (one per online CPU)
- [ ] Implement `refill_local_cache(order)`: take batch from buddy → local cache
- [ ] Implement `drain_local_cache(order)`: overflow → buddy
- [ ] Add watermark thresholds (KALLOC_PCPU_LOW, KALLOC_PCPU_HIGH in include/param.h)
- [ ] Trigger refill when allocating and local count < watermark
- [ ] Trigger drain when freeing and local count > watermark
- [ ] Export hit/miss and batch counts to /proc/vmstat

**Files Modified**:
- `kernel/vm/pagealloc.c` (add per-CPU layers)
- `include/param.h` (add watermark tuning constants)
- `kernel/fs/procfs.c` (add per-CPU cache stats)

**Validation**:
- [ ] Kernel builds
- [ ] `kallocstress -n 10` shows >10% improvement vs. Phase 2
- [ ] /proc/vmstat shows >90% cache hit rates on single-page allocs
- [ ] No lock contention on local operations

**Risk**: Medium (watermark tuning can cause regressions; validated via benchmarks)

---

### Phase 4: Address-Space Model (2 weeks)

**Goal**: Replace raw `pde_t *pgdir` with explicit `struct address_space` containing VMA tree

**Tasks**:
- [ ] Define `struct address_space` and `struct vaddr_range` in kernel/vm/vma.h
- [ ] Implement VMA operations: find, insert, remove, expand, shrink
- [ ] Rewrite `setupkvm()` to allocate and initialize address_space
- [ ] Rewrite `allocuvm()` → `vma_expand()`: add VMA entry, allocate pages
- [ ] Rewrite `deallocuvm()` → `vma_shrink()`: trim VMAs, drop page refs
- [ ] Rewrite `inituvm()` to use address_space; initialize child image
- [ ] Modify `struct proc` to hold `address_space *addrsp` instead of raw pgdir
- [ ] Update `switchuvm()` to use `p->addrsp->pgdir`
- [ ] Update VM traversal (`copyout`, `copyin`, `uva2ka`) to use address_space

**Files Modified**:
- `kernel/vm/vma.c` (NEW)
- `kernel/vm/vma.h` (NEW)
- `kernel/core/vm.c` (major rewrite of allocuvm, deallocuvm, inituvm, setupkvm)
- `kernel/core/proc.c` (update proc.addrsp initialization in allocproc/userinit)
- `kernel/core/exec.c` (update address-space setup for new binary)
- `include/proc.h` (add address_space field)

**Validation**:
- [ ] Kernel builds
- [ ] Guest boots to shell; login works
- [ ] Fork still works (still eager copy, no COW yet)
- [ ] `schedperf -n 3`, `kallocstress -n 3` show no regression
- [ ] `stackgrowtest` passes

**Risk**: Medium-High (large refactor of core VM data structures; correctness-critical)

---

### Phase 5: COW Mapping Install (1-2 weeks)

**Goal**: Parent-child fork shares pages; parent writable pages become read-only COW; child gets read-only mappings

**Tasks**:
- [ ] In `copyuvm()`, detect writable pages; call `install_cow_mapping()` instead of allocating child page
- [ ] `install_cow_mapping(parent_pa, child_pgdir, va, flags)`: incref parent page, map read-only COW to child
- [ ] Mark parent's mappings read-only (set PTE_COW or remove PTE_W)
- [ ] After child fork, flush parent TLB (invlpg or full reload)
- [ ] Add /proc/vmstat counter for COW mappings created
- [ ] Update phase-5 benchmark baseline

**Files Modified**:
- `kernel/core/vm.c` (refactor copyuvm, install_cow_mapping new func)
- `include/mmu.h` (define PTE_COW if not present)
- `kernel/fs/procfs.c` (add cow_mappings counter)

**Validation**:
- [ ] Kernel builds
- [ ] Fork succeeds; parent + child see same memory initially
- [ ] Parent writes trigger fault (next phase); for now, verify writes still work
- [ ] /proc/vmstat shows `cow_mappings` > 0 on fork
- [ ] `schedperf -n 3` fork count improves >30% (less copying)
- [ ] `kallocstress -n 3` fork count improves >20%

**Risk**: Medium (COW mappings must track correctly; refcount accuracy critical)

---

### Phase 6: Fault Dispatcher & COW Resolution (2 weeks)

**Goal**: Classify all page faults; resolve COW writes; demand-zero on first write to lazy page

**Tasks**:
- [ ] Refactor `trap.c` page-fault handler to classify fault type
- [ ] Implement `vm_handle_fault()` dispatcher in kernel/vm/fault.c
- [ ] Implement `fault_cow_resolve()`: 
  - Check refcount of page at faulted VA
  - If refcount > 1: allocate new page, copy content, decref old, update mapping
  - If refcount = 1: just make writable (upgrade from COW)
- [ ] Implement `fault_demand_zero()`: allocate page, zero, map writable
- [ ] Implement `fault_stack_growth()`: grow stack on appropriate faults
- [ ] Add fault counters to address_space (total, cow, demand_zero, stack_growth)
- [ ] Export to /proc/vmstat per-process fault statistics

**Files Modified**:
- `kernel/vm/fault.c` (NEW)
- `kernel/core/trap.c` (route page-fault to vm_handle_fault)
- `kernel/core/vm.c` (remove old stack-growth code; use new fault path)
- `kernel/fs/procfs.c` (add per-process fault counters)

**Validation**:
- [ ] Kernel builds
- [ ] Forked processes can write to their data; parent unaffected (COW works)
- [ ] `stackgrowtest` passes (stack growth via faults)
- [ ] `schedperf -n 5` shows improved fork-storm performance
- [ ] `/proc/pid/vmstat` shows breakdown of fault types
- [ ] `kallocstress -n 3` still passes

**Risk**: Medium-High (fault path is hot; COW refcount accuracy critical)

---

### Phase 7: Typed Object Allocator / UMA (1-2 weeks)

**Goal**: Stop wasting full pages for small objects; start with pipes

**Tasks**:
- [ ] Implement `struct kmem_cache` with per-object-size slab management
- [ ] Implement `kmem_cache_create()`, `kmem_cache_alloc()`, `kmem_cache_free()`
- [ ] Create `pipe_cache` with obj_size = sizeof(struct pipe)
- [ ] Update `kernel/core/pipe.c` to use kmem_cache_alloc/free
- [ ] Add per-cache stats: alloc_count, free_count, obj_count, waste
- [ ] Export to /proc/slabinfo style output

**Files Modified**:
- `kernel/vm/kmem_cache.c` (NEW)
- `kernel/vm/kmem_cache.h` (NEW)
- `kernel/core/pipe.c` (use pipe_cache)
- `kernel/fs/procfs.c` (add slabinfo output)

**Validation**:
- [ ] Kernel builds
- [ ] Pipes work correctly
- [ ] `pipe-page-churn` benchmark improves >30%
- [ ] Memory density improves: multiple pipes per page now
- [ ] No regression in pipe functionality or throughput

**Risk**: Low-Medium (isolated to pipe.c changes; cache logic is self-contained)

---

### Phase 8: Child-List Wait/Reap (1 week)

**Goal**: O(children) instead of O(NPROC) for parent-oriented operations

**Tasks**:
- [ ] Add parent, children, sibling fields to proc struct
- [ ] On fork: `link_child(parent, child)`
- [ ] On exit: `unlink_child(child)` or reparent to init
- [ ] Rewrite `wait()` and `waitpid()` to iterate `proc->children` instead of ptable
- [ ] Rewrite reparent logic to update child list on orphaning
- [ ] Add /proc/vmstat counter for wait/reap scan iterations

**Files Modified**:
- `kernel/core/proc.c` (major refactor of fork, exit, wait, waitpid)
- `include/proc.h` (add parent, children, sibling)

**Validation**:
- [ ] Kernel builds
- [ ] Wait/waitpid work; zombies cleaned up
- [ ] Reparenting to init on parent exit
- [ ] `schedperf -n 5` fork-heavy tests improve further
- [ ] Large process trees (>1000 procs) show O(children) behavior

**Risk**: Medium (process-tree correctness is critical; zombies must not leak)

---

### Phase 9: Lazy Swap & Page Reclaim (2-3 weeks, OPTIONAL TIER 1)

**Goal**: Add page eviction/swap when memory tight; enables larger workloads

**Tasks**:
- [ ] Implement swap device interface: `kernel/vm/swap.c`
- [ ] Implement page eviction daemon (kswapd equivalent): `kernel/vm/reclaim.c`
- [ ] Add CONFIG_SWAP tunable (default: disabled)
- [ ] Implement `fault_swapout_reload()`: page-in from swap on fault
- [ ] Track swap usage per address_space
- [ ] Add watermark-based reclaim trigger: when free_pages < watermark_low
- [ ] Implement page queue discipline: active, inactive, free
- [ ] Add /proc/vmstat: pages_swapped, swapins, swapouts, kswapd_events

**Files Modified**:
- `kernel/vm/swap.c` (NEW)
- `kernel/vm/reclaim.c` (NEW)
- `kernel/vm/pagealloc.c` (add watermark checks; kswapd wakeup)
- `kernel/core/proc.c` (track swap_usage in address_space)
- `kernel/fs/procfs.c` (add swap counters)
- `include/param.h` (add CONFIG_SWAP, swap device major)

**Validation** (if enabled):
- [ ] Kernel builds with CONFIG_SWAP=1
- [ ] System survives memory overcommit (allocate > phys RAM)
- [ ] Swap I/O works; pages page out and back in
- [ ] Performance degradation is graceful (not panic, not thrash)
- [ ] /proc/vmstat shows swap activity

**Risk**: Medium (adds new syscall paths; page lifecycle more complex; off by default)

---

### Phase 10: Observability & Hardening (1 week, ONGOING)

**Goal**: Production-grade observability; audit for remaining bugs

**Tasks**:
- [ ] Create comprehensive `/proc/vmstat` with 40+ counters
- [ ] Create `/proc/zoneinfo` (buddy allocator state per zone)
- [ ] Create `/proc/slabinfo` (cache statistics)
- [ ] Add per-address_space fault histograms
- [ ] Debug mode (CONFIG_VM_DEBUG): refcount over/underflow detection
- [ ] Add invariant checks throughout (can be compiled out)
- [ ] Document all counters in docs/vm_observability.md
- [ ] Create proc_vmstat_sample.txt with reference baseline output

**Files Modified**:
- `kernel/fs/procfs.c` (comprehensive stats export)
- `kernel/core/main.c` (initialize debug checks)
- `kernel/vm/pagedb.c` (add assertions)

**Validation**:
- [ ] All counters exported and sensible
- [ ] Baseline benchmarks run; output captured
- [ ] Invariant violations are caught early if they occur
- [ ] Documentation is clear and usable

**Risk**: Low (observability only; no functional changes)

---

## Invariants to Preserve (Document in Code)

These are the non-negotiables:

1. **No mapped+free**: No physical page shall be on a free list while still mapped in any active address space
2. **Refcount bounds**: refcount ≤ max mappings (e.g., refcount ≤ NPROC for user pages)
3. **PFN validity**: Every PFN in pg_array must have a valid descriptor with sensible flags
4. **TLB coherence**: After PTE transitions (writable→COW, etc.), TLB must be flushed before any mapping propagates
5. **Stack bounds**: Stack must not grow beyond `USER_STACK_MAX_PAGES` even under fault loops
6. **Zombie cleanup**: Exits must not leave zombies orphaned from wait scans (child list audit)
7. **VMA ordering**: VMAs must remain ordered by address; no overlaps
8. **COW semantics**: Parent write-protection and child COW marking must happen atomically (or be reverted together)

---

## Validation Checklist (All Phases)

After each phase completes, run this before merging:

### Mandatory Functional Tests
```bash
make clean && make aux.kern          # build must be clean
kallocstress -n 10                  # allocator stress
schedperf -n 10                     # scheduling stress
fsperf -n 10                        # filesystem stress
stackgrowtest                       # deep recursion + fork
```

### Mandatory Safety Tests (post-phase 5+)
```bash
memtest-large                       # allocate up to 80% RAM; verify no corruption
fork-bomb 256                       # fork 256 children; verify clean exit
exec-heavy 100                      # exec-heavy loop; verify address space cleanup
```

### Optional Stress Tests (recommended)
```bash
kallocstress -n 100                 # 100 iterations; overnight soak
pipe-page-churn                     # dedicated pipe allocator test (phase 7+)
```

---

## Performance Expectations by Phase

### Baseline (current system)
```
kallocstress:    85-90/100
schedperf:       82-86/100
fsperf:          85-88/100
stackgrowtest:   PASS
```

### Phase 2 (buddy allocator, no per-CPU)
```
Expected: ≥95% baseline (allocator changes isolated)
Risk: If <90%, rollback; investigate buddy correctness
```

### Phase 3 (per-CPU caches)
```
Expected: ≥100% baseline, prefer ≥105%
Risk: If <98%, watermark tuning needed
```

### Phase 4 (address_space model)
```
Expected: ≥98% baseline
Risk: If <95%, refactoring introduced overhead; investigate
```

### Phase 5 (COW install)
```
Expected: fork count ↓ 30-50% (less copying)
Risk: If fork still allocates as much, mapping install failed
```

### Phase 6 (fault dispatcher + COW resolution)
```
Expected: schedperf +10-20% (better fork throughput)
Risk: If fault overhead high, dispatcher is not optimized
```

### Phase 7 (UMA caches)
```
Expected: pipe-page-churn +30-50%; kallocstress +5-10%
Risk: If overhead dominates, slab logic needs optimization
```

### Phase 8 (child-list wait)
```
Expected: schedperf +5-10% on fork-heavy workloads
Risk: If regressions, child-list traversal not improving O(NPROC)
```

### Phase 10 (final)
```
Target: kallocstress 90+, schedperf 87+, fsperf 87+
(Phase 4-10 combined should yield visible improvement vs. baseline)
```

---

## Risk Mitigation Strategy

### High-Risk Areas & Mitigations

| Area | Risk | Mitigation |
|------|------|-----------|
| Buddy allocator | Free-list corruption | Unit tests; invariant checks; stress test on stable phase 1 |
| COW refcount | Underflow/overflow | Debug mode (CONFIG_VM_DEBUG); audit every incref/decref |
| Address space lifecycle | UAF or double-free | Spin lock on address_space; ref count before destroy |
| Fault handler | Lost faults or infinite loops | Enumerate all fault types; add max-retry counter |
| Child-list reparent | Orphaned zombies | Validate after each reparent; audit wait/reap logic |
| TLB coherence | Stale writable mappings after COW | Flush after every PTE update; use invlpg for single entries |

### Rollback Triggers (Automatic Halt & Investigate)

- Free-page count drifts downward over 5+ boot cycles (allocator leak)
- Any benchmark regresses >10% from prior phase baseline
- Segmentation fault or kernel panic in guest
- Invariant check fires (CONFIG_VM_DEBUG mode)
- Process hang or deadlock detected (watchdog timeout)

### Rollback Procedure

1. Revert to last-known-good commit
2. Run validation suite (must return to baseline)
3. Document failure mode in `docs/rollback-postmortem-<date>.md`
4. Investigate root cause; fix; resubmit with additional guards

---

## Compatibility Notes

### Retained / Compatible

- External `kalloc()`/`kfree()` API shape (layer 1 wraps buddy)
- x86 page table format (no changes to PTE bits except PTE_COW)
- Device DMA APIs (updated to use layer 1; functionally same)
- /proc interface (extended with new counters, not broken)

### Changed / Breaking

- `struct proc` layout (add address_space field) — affects gdb/tools
- Process creation flow (fork uses COW) — expected behavioral change
- Address-space cleanup on exit (drops page refs; may trigger reclaim) — expected

### Not Supported (yet)

- NUMA-aware allocation (reserved field in layer 0; phase 11+)
- Live memory migration (defrag) — not planned; not needed
- Shared memory segments (SHM) beyond normal COW — layer 2 can support; not implemented
- Swap to compressed memory (zswap) — swap is simple I/O; no compression

---

## Next Steps / Immediate Actions

1. **Create implementation branch**: `git checkout -b vm-ng-phase0`
2. **Lock in baseline measurements**: Run validation suite; save to `docs/measurements-baseline-2026-04.md`
3. **Design review**: Phase 0-2 architecture with team; validate invariant assumptions
4. **Assign ownership**: Phases 0-3 critical path; phases 4-10 can parallelize later

---

## References

- [auxv6 Allocator-VM Refactor Blueprint](allocator-vm-refactor-blueprint.md) — prior design work
- [Linux kernel memory management](https://www.kernel.org/doc/html/latest/core-api/memory-management.html) — reference
- [FreeBSD UMA design](https://papers.freebsd.org/2001/uma/) — typed cache inspiration
- [Darwin VM architecture](https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/KernelProgramming/Mach/Mach.html) — COW precedent
- [OpenBSD pv_entry tracking](https://man.openbsd.org/pmap.9) — simple reference model

---

## Durability Note

If conversation context is compacted, recovery information:

- **Start here**: This file (docs/new_vm_structure.md)
- **Then read**: docs/allocator-vm-refactor-blueprint.md (prior phases)
- **Timeline**: 12-14 weeks for full implementation serial; 8-10 weeks with phase parallelization
- **Checkpoints**: Phase 3 (allocator foundation), Phase 6 (COW working), Phase 10 (prod-ready)

---

## Appendix: Quick Reference

### Phases at a Glance

| Phase | Name | Duration | Goal | Exit Criteria |
|-------|------|----------|------|---------------|
| 0 | Measurement | 1w | Baseline lock-in | Benchmarks recorded |
| 1 | Page DB | 1w | PFN metadata | Build clean, no regression |
| 2 | Buddy | 1-2w | Allocator foundation | ≥95% perf vs baseline |
| 3 | Per-CPU | 1w | Fast paths | Cache hit >90% |
| 4 | Address-space | 2w | VM model rewrite | Fork works, no regression |
| 5 | COW install | 1-2w | Shared fork | Fork allocations ↓30% |
| 6 | Fault dispatch | 2w | COW resolution | schedperf +10% |
| 7 | UMA caches | 1-2w | Small objects | pipe-churn +30% |
| 8 | Child lists | 1w | Fast wait/reap | schedperf +5% |
| 9 | Swap/reclaim | 2-3w | Optional tier 1 | Handles OOM gracefully |
| 10 | Observability | 1w | Production ready | 40+ counters exported |

### Key File Mapping

```
page descriptor ←→ kernel/vm/page.h, kernel/vm/pagedb.c
buddy allocator ←→ kernel/vm/buddy.c, kernel/vm/pagealloc.c
address_space   ←→ kernel/vm/vma.c, kernel/vm/vma.h
fault handling  ←→ kernel/vm/fault.c
typed caches    ←→ kernel/vm/kmem_cache.c
child tracking  ←→ kernel/core/proc.c
```

---

**Document Version**: 1.0  
**Created**: April 10, 2026  
**Last Updated**: April 10, 2026  
**Status**: Architecture complete; ready for phase 0 implementation
