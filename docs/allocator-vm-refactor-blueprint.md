# Allocator and VM Refactor Blueprint

Date: 2026-04-04
Status: Phase 1 validated; Phase 2c validated and promoted as working baseline
Scope: kernel page allocator, VM fault path, fork semantics, kernel object allocation, process reaping bookkeeping, observability

## Why This Document Exists

This document is intentionally redundant with the roadmap and kernel performance
notes.  The redundancy is deliberate.

The allocator and VM subsystem is now important enough that the plan must remain
recoverable after context compaction, branch drift, or partial implementation.
This file is the durable reference for what is wrong, what modern kernels do,
what auxv6 should do, which files are in scope, and what order minimizes risk.

## Executive Summary

auxv6 is still carrying xv6-era allocator and address-space assumptions into a
much larger kernel:

- one global free-page list remains the true allocator bottleneck
- small per-CPU caches reduce lock traffic but do not change the architecture
- `fork()` still performs eager dense address-space copying via `copyuvm()`
- page faults are still treated as exceptions with one special stack-growth path
- hot kernel objects still consume whole pages when a typed cache would suffice
- parent-child reap paths still scan the entire process table repeatedly

That combination explains why the system can look acceptable in narrow paths but
collapse under allocator- and fork-heavy benchmarks like `kallocstress`.

The right correction is not another local optimization.  The right correction is
a production-style split of responsibilities:

1. physical page allocator with per-page metadata and per-CPU fast paths
2. VM mapping and fault machinery with explicit copy-on-write semantics
3. typed object allocator for small kernel objects
4. process bookkeeping that does not rescan global tables in the hot path
5. procfs observability for allocator and VM state transitions

## Current Landing Status

Phase 1 is now in-tree.

What landed:

- PFN-indexed page metadata in `kernel/core/kalloc.c`
- managed-page refcount groundwork
- `kfree()` now behaves as a refcounted release for managed pages and only
	returns a page to the free allocator when the count reaches zero
- exact allocator-side counters for alloc/free calls, per-CPU cache hits and
	misses, refill and drain batches, and deferred frees
- `kalloc_stats()` snapshot helper and basic page-refcount helper interfaces
- `/proc/vmstat` with allocator counters
- `/proc/meminfo` extended with page-count lines (`PagesTotal`, `PagesFree`,
	`PagesAlloc`)

What explicitly did not land yet:

- no copy-on-write fork semantics
- no lazy heap allocation
- no typed slab or zone allocator
- no child-list reaping conversion

Current validation state:

- kernel builds cleanly
- guest validation passed on 2026-04-04:
	- `kallocstress -n 3`: `88/100` avg
	- `schedperf -n 3`: `83/100` avg
	- `fsperf -n 3`: `86/100` avg

Phase 1 assessment:

- The tranche is successful.
- It restored the system to the historical best band without introducing the
	sparse-fork regression pattern.
- The new observability is already useful and matches expectations for a pre-COW
	kernel.

Observed `/proc/vmstat` interpretation from first validation:

- `pages_shared 0` is expected because no COW or other shared-page semantics have
	landed yet.
- `ref_increments 0` is expected for the same reason; no current path is calling
	page-sharing incref helpers in normal operation.
- `deferred_frees 0` is expected because managed-page references are still almost
	always single-owner.
- `cache_alloc_hits` greatly exceed `cache_alloc_misses`, which confirms the
	current stash-based fast path is active and working.
- `alloc_calls` is not equal to `cache_alloc_hits + cache_alloc_misses` because
	some allocations happen during early boot before the post-boot per-CPU cache
	path is active.
- `free_calls` is much larger than `alloc_calls` because the counters include the
	initial population of managed memory via `freerange()` and subsequent frees.

## Public Design Baselines Reviewed

This plan is based on public design patterns, not private or leaked code.

### Linux

- page metadata (`struct page`) indexed by physical frame
- global buddy allocator behind per-CPU page caches/pagesets
- page-table helper discipline for present/write/protection transitions
- first-class fault handling for demand allocation and copy-on-write
- reserves and watermarks to protect critical allocations

### FreeBSD

- `vm_page_t` as central physical-page metadata object
- VM object and fault model separated from raw page allocation
- UMA slab or zone allocator for typed kernel objects with per-CPU caches
- cache reclaim and occupancy tracking built into allocator design

### NetBSD UVM

- explicit VM fault classes and page-database model
- page daemon and reserve-aware physical page allocation semantics
- COW-aware process VM operations
- exported VM counters as part of the system interface

## Current auxv6 Problems by Mechanism

### 1. Page allocation remains globally serialized

Current implementation in `kernel/core/kalloc.c`:

- one shared freelist protected by `kmem.lock`
- a fixed-size per-CPU stash (`KALLOC_CPU_CACHE=32`)
- refill and drain still funnel through the single global list

That means the system still behaves like a global allocator with a small local
buffer, not like a true SMP-scalable allocator.

### 2. `fork()` still scales with mapped memory size

Current implementation in `kernel/core/proc.c` and `kernel/core/vm.c`:

- `fork()` calls `copyuvm()` before the child runs
- `copyuvm()` allocates and copies every mapped page in the parent image
- allocator pressure and memory bandwidth scale with address-space footprint

This is precisely what modern kernels avoid with COW fork.

### 3. Small kernel objects are over-allocated

Current implementation example in `kernel/core/pipe.c`:

- `struct pipe` contains a 512-byte data ring
- the pipe object is still allocated with `kalloc()`
- one pipe therefore consumes a whole 4 KiB page

That amplifies allocator churn in `pipe-page-churn` and wastes physical pages.

### 4. Wait/reap still rescans global state

Current implementation in `kernel/core/proc.c`:

- `proc_waitpid()` iterates over all `ptable.proc[]`
- parent-child relationships are discovered by global scans instead of local lists
- fork-heavy workloads therefore pay repeated O(`NPROC`) scans

### 5. Fault handling is too narrow

Current implementation in `kernel/core/trap.c` and `kernel/core/proc.c`:

- page faults are mostly fatal
- one narrow stack-growth path exists
- there is no first-class fault taxonomy for demand-zero, COW, or protection repair

That blocks modern fork and lazy-allocation behavior.

## Design Principles for the Refactor

These principles should remain true even if implementation details evolve.

### Principle 1: Separate page allocation from object allocation

Physical pages and fixed-size kernel objects are different problem domains.
`kalloc()` should not remain the default allocator for every kernel structure.

### Principle 2: Make per-page metadata explicit

If the kernel wants refcounted sharing, reclaim, zero-page reuse, or fault-driven
copy-on-write, physical pages need metadata outside of page tables.

### Principle 3: Optimize the common SMP fast path locally

Single-page alloc/free operations should usually complete on the local CPU
without taking a global allocator lock.

### Principle 4: Use faults to defer expensive work

Eager copying at fork is the wrong place to spend cycles.  Fault handlers should
be the slow path for uncommon writes, not the fast path for every fork.

### Principle 5: Make state transitions observable

If the allocator or VM layer changes state, there should be counters or procfs
visibility to confirm it happened and to detect regressions quickly.

## Target Subsystem Shape

### Layer A: Page metadata database

Introduce a PFN-indexed metadata table for all managed physical pages.

Minimum fields needed in the first tranche:

- `refcount`
- allocator flags
- free or cached state
- owner classification or allocation class
- zeroed hint

Likely future fields:

- queue or list links
- fault or reclaim statistics
- migration or movable classification
- debugging provenance when compiled in

### Layer B: Per-CPU page allocator

Replace the current stash model with a real per-CPU page cache design:

- per-CPU free-page list or magazine
- high and low watermarks
- batched refill from a shared allocator
- batched drain back to shared allocator
- emergency reserve for kernel-critical paths

This does not need NUMA for the first tranche.  UMA plus per-CPU batching is
already a major architectural upgrade.

### Layer C: Shared page mappings and COW

Fork should stop allocating private physical pages for every mapping up front.

New behavior target:

- parent and child share physical pages after `fork()`
- writable user mappings become read-only COW mappings in both address spaces
- physical page refcount increments on shared mapping install
- first writer takes a page fault
- fault handler allocates a replacement page only if write sharing still exists

### Layer D: Typed object caches

Introduce a small fixed-size object allocator backed by pages from the page
allocator.  The first purpose is not elegance; it is to stop wasting whole pages
for small hot objects.

First migration candidates:

- `struct pipe`
- small VFS helper objects
- network control objects
- future adjunct metadata objects created frequently and freed frequently

### Layer E: Process-local child bookkeeping

Add child lists to `struct proc` so parent-oriented operations become local.

Desired outcomes:

- `wait()` and `waitpid()` iterate children, not the global process table
- reparenting updates child lists explicitly
- zombie reap work becomes proportional to a parent's children rather than all slots

### Layer F: Observability

Before or with semantic changes, export counters for:

- page alloc fast-path hits and misses
- batch refill and drain counts
- reserve allocations
- allocation failures
- COW mappings created
- COW faults handled
- zero-fill faults
- stack-growth faults
- wait or reap scan counts before and after child-list conversion

## File-by-File Impact Map

### Headers

`include/proc.h`
- child list linkage
- optional VM context pointer replacing or supplementing raw `pgdir`
- per-process VM accounting fields

`include/defs.h`
- page metadata helpers
- allocator interfaces
- VM helper interfaces
- typed object allocator interfaces

`include/mmu.h`
- software-defined PTE bit discipline for COW and related VM state
- helper macros or inline functions for PTE transitions

`include/param.h`
- allocator cache or watermark tuning constants
- reserve sizing policy

### Core allocator and VM

`kernel/core/kalloc.c`
- split raw allocator policy from implementation details
- add page metadata maintenance
- add per-CPU list or magazine management
- add reserve accounting and procfs-visible counters

`kernel/core/vm.c`
- add page refcount operations
- change deallocation to drop refs rather than always freeing pages
- introduce COW mapping install path
- add page-fault resolution helpers
- add targeted TLB invalidation helpers

`kernel/core/trap.c`
- route page faults through a VM-aware dispatcher
- classify fault types instead of hard-coding only stack growth

`kernel/core/exec.c`
- build initial address-space state compatible with future lazy and COW semantics

`kernel/core/sysproc.c`
- prepare `sbrk()` or `growproc()` for future lazy heap growth

### Process bookkeeping

`kernel/core/proc.c`
- rework `fork()` around shared mappings
- rework `exit()` and reparenting around child lists
- rework `wait()` and `waitpid()` around local child iteration
- clean VM lifetime transitions for process teardown

### Hot object users

`kernel/core/pipe.c`
- first typed-object allocator client
- should stop allocating one page per pipe object

`kernel/fs/*`
- audit for small hot objects that still use full pages indirectly or directly

`kernel/net/*`
- same audit for network-side control objects and small buffers

### Observability

`kernel/fs/procfs.c`
- export allocator and VM counters
- avoid large stack-local buffers while doing so

## Refactor Phases

### Phase 0: measurement lock-in

Do not start the semantic refactor without a stable measurement contract.

Keep using:

- `kallocstress`
- `schedperf`
- `fsperf`
- `stackgrowtest`

Add procfs counters as early as possible so these tests can be correlated with
allocator and VM behavior.

### Phase 1: page metadata with no behavioral change

Purpose:
- create the foundation required by later COW and allocator changes

Allowed changes:
- PFN metadata table
- refcount initialization and maintenance
- zeroed flag or basic page flags
- procfs counters

Disallowed changes in this phase:
- changing fork semantics
- changing page fault behavior
- introducing lazy heap allocation

Exit criteria:
- kernel builds cleanly
- no functional regressions
- counters report sensible totals during boot and workload execution

Implementation status (2026-04-04):
- landed in-tree
- build clean
- guest validation passed

### Phase 2: real per-CPU page allocator

Purpose:
- remove single-lock pressure from the common alloc/free path

Allowed changes:
- replace ad hoc stash with per-CPU magazines or local lists
- add refill or drain batches controlled by watermarks
- preserve external `kalloc()` and `kfree()` API shape

Exit criteria:
- `kallocstress` improves in `pipe-page-churn` and allocator-reclaim paths
- no free-page leakage
- procfs counters show most single-page activity is local

Implementation status (2026-04-04):
- landed in-tree
- build clean
- guest validation complete (regression vs Phase 1 baseline)
- policy now uses explicit watermarks and batch tunables in `include/param.h`:
	- `KALLOC_PCPU_LOW_WATER`
	- `KALLOC_PCPU_HIGH_WATER`
	- `KALLOC_REFILL_BATCH`
	- `KALLOC_DRAIN_BATCH`
	- `KALLOC_GLOBAL_RESERVE`
- allocator now uses explicit local refill/drain helpers in `kernel/core/kalloc.c`:
	- refill local cache when below low watermark
	- drain local cache when above high watermark
	- move pages between local and global pools in bounded batches
	- keep external `kalloc()` / `kfree()` API unchanged

Validation snapshot (2026-04-04, first Phase 2 run):
- `/proc/vmstat` sample:
	- `cache_alloc_hits 6666`
	- `cache_alloc_misses 0`
	- `global_refill_batches 411`
	- `global_refill_pages 6576`
	- `global_drain_batches 294`
	- `global_drain_pages 4704`
- Benchmarks:
	- `kallocstress -n 3`: `83/100` avg (Phase 1 baseline was `88/100`)
	- `schedperf -n 3`: `81/100` avg (Phase 1 baseline was `83/100`)
	- `fsperf -n 3`: `84/100` avg (Phase 1 baseline was `86/100`)

Assessment of this first Phase 2 policy:
- Functional correctness is good and all suites still pass.
- Performance regressed enough versus Phase 1 baseline that current Phase 2
	tunables should not be considered final.
- Likely mechanism: over-eager low-water refill causes too-frequent global-lock
	acquisitions even when local cache still had usable pages. `cache_alloc_misses`
	being exactly zero supports this interpretation.

Decision:
- Keep Phase 2 architecture changes.
- Retune Phase 2 policy (watermarks and refill trigger) before advancing to
	Phase 3.

Phase 2b retune incident and rollback status (2026-04-04):
- Phase 2b landed in-tree and built clean, but produced a login-path kernel panic in guest.
- The panic was triaged and the 2b policy-only changes were rolled back, keeping
	Phase 2 architecture while restoring prior tuning behavior.
- Post-rollback build is clean and guest validation is stable.

Post-rollback validation snapshot (2026-04-04):
- `/proc/vmstat` sample:
	- `cache_alloc_hits 7134`
	- `cache_alloc_misses 0`
	- `global_refill_batches 438`
	- `global_refill_pages 7008`
	- `global_drain_batches 321`
	- `global_drain_pages 5136`
- Benchmarks:
	- `kallocstress -n 3`: `82/100` avg (min 82, max 83)
	- `schedperf -n 3`: `81/100` avg (min 80, max 82)
	- `fsperf -n 3`: `83/100` avg (min 81, max 85)

Assessment update:
- Stability is restored and all suites pass.
- Performance remains below Phase 1 baseline, so current Phase 2 policy is
	not yet the preferred long-term baseline for throughput.
- Keep rollback state as the safe checkpoint while designing a narrower, safer
	retune with explicit rollback guardrails.

Phase 2c retune status (2026-04-04):
- landed in-tree
- build clean and guest validation complete
- single-lever policy update applied:
	- keep `KALLOC_PCPU_LOW_WATER`, `KALLOC_PCPU_HIGH_WATER`,
	  `KALLOC_REFILL_BATCH`, and `KALLOC_DRAIN_BATCH` unchanged
	- add `KALLOC_PCPU_REFILL_TRIGGER` and only do preemptive refill once local
	  cache reaches that lower threshold
- validation snapshot:
	- `/proc/vmstat`: `cache_alloc_hits 6669`, `cache_alloc_misses 0`,
	  `global_refill_batches 405`, `global_refill_pages 6480`,
	  `global_drain_batches 288`, `global_drain_pages 4608`
	- `kallocstress -n 3`: `84/100` avg (83-85)
	- `schedperf -n 3`: `81/100` avg (81-81)
	- `fsperf -n 3`: `86/100` avg (86-86)
- delta vs rollback checkpoint (`82/81/83`):
	- `kallocstress +2`
	- `schedperf +0`
	- `fsperf +3`
	- refill/drain batch counters reduced in `/proc/vmstat`
- decision:
	- promote Phase 2c as the current safe performance baseline
	- keep policy fixed while Phase 3 shared-page VM scaffolding is brought up

### Phase 3: VM scaffolding for shared pages

Purpose:
- make `freevm()` and `deallocuvm()` safe for shared physical pages

Allowed changes:
- page ref decrement logic
- PTE helper layer
- helper functions for wrprotect, present, user, and COW transitions

Disallowed changes:
- full COW fork enablement until teardown paths are proven correct

Exit criteria:
- no double-free or leaked-page regressions
- dense fork still works unchanged functionally

Implementation status (2026-04-04, tranche 1):
- started in-tree
- added software-defined COW marker bit in `include/mmu.h` (`PTE_COW`)
- added VM transition helpers in `kernel/core/vm.c`:
	- `pte_is_cow()`, `pte_is_writable()`
	- `pte_mark_cow()`, `pte_mark_writable()`
	- `uvm_release_pte()`
- routed `deallocuvm()` through `uvm_release_pte()` so user-page teardown uses
	a single refcount-safe release path ahead of COW mapping work
- explicit scope guard: no copy-on-write fork behavior enabled yet
- guest validation complete with no regression/panic:
	- `kallocstress -n 3`: `85/100` avg (85-85)
	- `schedperf -n 3`: `81/100` avg (81-82)
	- `fsperf -n 3`: `85/100` avg (85-86)
	- `/proc/vmstat`: `cache_alloc_hits 6669`, `cache_alloc_misses 0`,
	  `global_refill_batches 405`, `global_drain_batches 288`
- tranche-1 decision: accepted; proceed to tranche-2 invariants before enabling
	any COW fork mapping semantics

Implementation status (2026-04-04, tranche 2):
- landed in-tree
- build clean on host (`sudo make aux.kern`)
- helperized additional transition paths and invariants:
	- added `pte_is_user()` and `pte_mark_user()` helpers
	- added PTE sanity checks to guard illegal writable+COW combinations in
	  transition-sensitive VM paths
	- routed `clearpteu()`, `setpteu()`, `user_page_state()`, and `uva2ka()`
	  through helper/invariant-aware flow
	- added invariant checks in `copyuvm()` to keep current dense-fork path
	  clean while COW is still disabled
- explicit scope guard: COW fork install/map semantics are still not enabled

### Phase 4: copy-on-write fork

Purpose:
- replace eager copying in `fork()` with shared read-only mappings

Required behavior:
- parent and child both get correct semantics after writes
- `exec()` and exit teardown handle shared pages correctly
- stack growth remains correct

Exit criteria:
- `schedperf` fork-storm rises materially
- `kallocstress` fork-copyuvm rises materially
- no regression in `stackgrowtest`

Implementation status (2026-04-04, slice 1):
- landed in-tree
- host build clean (`sudo make aux.kern`)
- first COW semantic slice enabled:
	- `copyuvm()` now shares writable managed user pages as read-only COW mappings
	  and bumps physical-page refcounts
	- parent writable user mappings are write-protected for COW after child map
	  install succeeds
	- trap page-fault path calls `cow_fault()` before stack-growth fallback
	- `cow_fault()` resolves COW writes by either private-copying shared pages or
	  restoring writability when refcount is 1
	- fork path refreshes parent TLB view after potential parent write-protect
	  changes
- safety guard:
	- COW remains scoped to writable managed user pages only in this slice
	- further mapping classes stay on dense-copy path until guest validation passes
- next gate:
	- completed: guest stability and perf validation passed
	- `/proc/vmstat` confirms expected COW activity (`ref_increments 172`,
	  `deferred_frees 172`)
	- `kallocstress -n 3`: `84/100` avg
	- `schedperf -n 3`: `82/100` avg
	- `fsperf -n 3`: one low outlier run; confirmation rerun `fsperf -n 5`
	  stabilized at `85/100` avg
	- decision: promote slice-1 and proceed to slice-2 scope expansion

Implementation status (2026-04-04, slice 2):
- landed in-tree
- host build clean (`sudo make aux.kern`)
- correctness and scope updates:
	- page-fault COW resolution now runs only on user write-protection faults
	  (`present=1`, `write=1`) before stack-growth fallback
	- `copyuvm()` now directly shares read-only managed user mappings with
	  refcount bumps instead of dense-copying them
- safety guard:
	- writable managed user mappings still use explicit COW path
	- unmanaged or non-user mappings remain on dense-copy behavior
- next gate:
	- completed with strong results:
	  - `/proc/vmstat`: `ref_increments 371`, `deferred_frees 371`
	  - `kallocstress -n 3`: `92/100` avg
	  - `schedperf -n 3`: `87/100` avg
	  - `fsperf -n 3`: `87/100` avg
	- decision: promote slice-2 as current best baseline; pause scope expansion
	  and harden correctness/coverage around current COW behavior first

### Phase 5: child-list wait and reap

Purpose:
- remove repeated full process-table scans during parent-child operations

Exit criteria:
- `schedperf` fork-heavy subtests improve or at minimum stop being scan-dominated
- reparenting and zombie cleanup remain correct

### Phase 6: typed object allocator

Purpose:
- stop using full pages for small hot objects

First target:
- `struct pipe`

Why first:
- directly exercised by `pipe-page-churn`
- object is fixed-size and self-contained
- good proof of allocator layering

Exit criteria:
- measurable `pipe-page-churn` improvement
- lower page churn visible in procfs counters

## Invariants to Preserve

These invariants should be documented in code comments where relevant.

- no physical page may be returned to a free list while still mapped in any live address space
- refcount transitions must be explicit and auditable
- page-fault handlers must not silently convert invalid faults into successful accesses
- stack growth must remain bounded by `USER_STACK_MAX_PAGES`
- child reparenting must never orphan zombies from the wait path
- procfs observability must not require large kernel-stack allocations

## Regression Risks

### High-risk areas

- page refcount underflow or double-free
- stale writable mappings after COW conversion
- TLB stale-entry bugs after PTE permission changes
- stack growth interaction with COW and reserve pages
- process teardown with shared pages still referenced elsewhere
- typed caches accidentally bypassing allocator accounting

### Why the sparse-fork experiment failed

This failure should remain documented because it is the clearest warning about
half-steps in this subsystem.

Sparse fork removed pre-mapped reserve stack pages without introducing a general
VM fault model.  That forced deeper child stack usage onto a repeated
page-fault-plus-allocation path and tanked throughput.  The lesson is not that
fault-based VM is wrong.  The lesson is that fault-based VM must be designed as
an integrated subsystem, not introduced as one isolated shortcut in `copyuvm()`.

## Validation Matrix

### Mandatory tests after every phase

- build kernel cleanly
- boot guest manually
- `stackgrowtest`
- `schedperf -n 3`
- `kallocstress -n 3`

### Mandatory tests after allocator phases

- `kallocstress -n 3`
- `fsperf -n 3` to catch cross-subsystem allocator fallout

### Mandatory tests after COW fork phase

- `schedperf -n 3`
- `kallocstress -n 3`
- `stackgrowtest`
- representative userland smoke: shell, login path, simple exec-heavy commands

### Mandatory tests after typed allocator introduction

- `kallocstress`
- pipe-heavy benchmarks or focused pipe smoke
- open or close churn if file objects are migrated later

## Rollback Rules

Rollback a phase if any of the following occurs:

- free page counts drift downward across repeated steady-state test loops
- fork or exit correctness regresses even if scores rise
- stack growth becomes fault-fragile again
- procfs observability becomes misleading or too expensive
- improvements in one benchmark are purchased by obvious collapse in the others

The sparse-fork rollback is the precedent: correctness first, then architecture,
then tuning.

## What Not To Do

- do not keep tuning `KALLOC_CPU_CACHE` as if that were a durable solution
- do not attempt another sparse-fork shortcut without a complete COW-capable fault path
- do not add more page-allocation special cases in individual callers instead of fixing allocator architecture
- do not mix typed object allocator work with page-refcount semantics unless the measurement plan is already in place

## Most Defensible First Implementation Tranche

The first tranche should be:

1. PFN-indexed page metadata table
2. page refcount helpers with no semantic fork change yet
3. allocator and VM procfs counters

Reason:

- it creates the prerequisites for COW and typed allocators
- it is lower risk than changing fork semantics immediately
- it turns future regressions into observable events instead of archaeology

## Durability Note

If conversation context is compacted, the intended recovery order is:

1. read this file
2. read `docs/kernel-perf-hardening.md`
3. read `docs/ROADMAP.md`
4. read `/memories/repo/kalloc-redesign-reset-2026-04-04.md`

That is the canonical re-entry path for this subsystem refactor.