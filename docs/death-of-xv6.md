# Death of xv6: Kernel Core Modernization Notes

Date: 2026-04-05
Status: Landed (build-clean)
Scope: Kernel-core P0 stability and large-file correctness tranche

## Why this document exists

auxv6 started from xv6 foundations, but the current system has outgrown a number
of implicit 32-bit and fixed-limit assumptions. This document records the first
hard modernization tranche that removes high-risk xv6-era core constraints.

This is not a filesystem-feature document. It is a kernel-core correctness and
ABI-integrity document focused on offsets, file sizes, VFS dispatch, and PID
allocation behavior.

## What was modernized in this tranche

### 1) 64-bit file offset and file size core state

The following kernel/core ABI surfaces were widened so file offsets and sizes are
not silently truncated at 4 GB:

- `off_t` is now `int64_t`.
- `struct stat.st_size` is now 64-bit.
- `struct file.off` is now `uint64_t`.
- In-memory `struct inode.size` is now `uint64_t`.

Rationale:

- Old xv6-style `uint` offsets/sizes create silent wraparound and corruption
  hazards once workloads exceed 4 GB.
- Modern Unix-like behavior requires large-file-safe internal accounting even on
  32-bit architectures.

### 2) End-to-end 64-bit offset plumbing through VFS and device paths

Offset-bearing callback signatures were widened to 64-bit, including:

- `readi`/`writei`/`procfs_readi`
- device-switch read/write callbacks
- vnode/VFS read/write callbacks

All in-tree filesystem backends were signature-aligned in the same pass so VFS
cannot truncate offsets while hopping between backend implementations.

Rationale:

- Partial widening is a footgun. If only top-level types are widened but callback
  signatures remain 32-bit, truncation still occurs at dispatch boundaries.

### 3) Removal of explicit 4 GB truncation clamps in backend paths

Several backends had explicit cast-or-clamp behavior that forced large sizes down
to 32-bit values. These paths were removed or corrected where safe.

Rationale:

- Explicit truncation masks bugs and produces incorrect user-visible metadata.
- Large-file behavior must fail explicitly when unsupported, not silently lie.

### 4) PID overflow/wrap hardening

Old behavior used a monotonic `nextpid++` with no wrap policy. New behavior:

- Adds explicit `PID_MAX` policy.
- Wraps safely to PID 2 (preserving PID 1 semantics).
- Skips in-use PIDs while holding `ptable.lock`.
- Panics only on true PID-space exhaustion.

Rationale:

- Unchecked signed overflow in process IDs is a long-tail correctness and safety
  risk for signal/wait paths.

## Compatibility and architecture notes

### i386 syscall ABI caveat (known follow-up)

The kernel internals are now large-file clean for offsets/sizes, but current
`sys_lseek` userspace argument width on i386 is still 32-bit.

Implication:

- Relative movement in a single seek syscall remains bounded by 32-bit argument
  width.
- Full-range userspace seek parity requires a dedicated 64-bit seek syscall
  (`sys_lseek64` or `_llseek`-style split-argument ABI).

### Why msdosfs keeps some 32-bit arithmetic

msdosfs cluster traversal currently keeps key division/modulo math in 32-bit
space intentionally to avoid pulling compiler runtime 64-bit division helpers
into kernel link (`__udivmoddi4`) in this toolchain/link model.

This is an explicit, documented boundary in this tranche, not an accidental
leftover.

## Validation snapshot

- `sudo make aux.kern` passes after the full migration set.
- The link stage completes successfully (no unresolved helper symbols in final
  kernel link for this tranche).

## Early-boot hardening follow-up (2026-04-05)

After descriptor-ceiling experimentation exposed a boot-stability sensitivity,
early-boot behavior was hardened so static-size growth fails safely.

### Hardening changes

- Early entry page-table window increased from 4MB to 8MB (two 4MB PDEs) in
  `entrypgdir`.
- `kinit1/kinit2` split now uses `BOOT_EARLY_PHYSTOP` from `memlayout.h`
  instead of a scattered literal.
- Linker script now enforces a hard budget with `ASSERT`:
  kernel image end must fit inside the early mapped window.
- Added compile-time static table-budget guards in core subsystems:
  - process table (`sizeof(ptable.proc) <= PROC_TABLE_BYTES_MAX`)
  - global file table (`sizeof(ftable.file) <= FILE_TABLE_BYTES_MAX`)
  - page-metadata table (`sizeof(kpage_meta) <= KPAGE_META_BYTES_MAX`)
- Added post-link `aux.kern` budget checks in the Makefile:
  - hard fail if total kernel size exceeds 8MB
  - hard fail if `.bss` exceeds 4MB
  - soft warning if total kernel size exceeds 6MB
  - print current footprint on every kernel link

### Why this matters

- Previously, growth in static data could push the image beyond the pre-kvmalloc
  mapping and trigger very-early faults (before useful console output).
- With the link-time assertion, this class of failure is detected at build time
  instead of surfacing as a silent bootloop.
- With the static-table and post-link checks, growth regressions are now caught
  in normal build flow instead of depending on runtime discovery.

## What this tranche does NOT claim

- It does not complete userspace 64-bit seek ABI parity.
- It does not attempt to modernize all historical scheduler/process-table design
  choices in one step.
- It does not claim every backend now supports arbitrarily large files; it
  removes silent truncation and improves type correctness in shared core paths.

## P1 progress (descriptor ceilings)

Status: Deferred (2026-04-05).

P1-A descriptor-ceiling bumps were attempted, built cleanly, and then rolled
back pending boot-stability isolation after a reported early-boot regression.
The values were returned to the prior baseline while regression triage proceeds.

## P1 progress (pipe policy)

Status: Landed (2026-04-05), build-clean.

P1-B modernizes pipe buffering while preserving POSIX-visible atomicity rules.

### P1-B changes

- Kernel pipe ring capacity increased from `512` to `2048` bytes via
  `PIPE_CAPACITY` in `param.h`.
- POSIX `PIPE_BUF` remains `512` (atomic write floor) in `sys/param.h`.
- Added compile-time invariants:
  - `PIPE_CAPACITY >= 512`
  - `sizeof(struct pipe) <= PGSIZE` (pipe objects are one-page `kalloc()`)

### Why this split is intentional

- Capacity and atomicity are distinct policies:
  - larger kernel capacity improves throughput and reduces writer wake/sleep
    churn under pipelines;
  - `PIPE_BUF` stability preserves expected atomic-write semantics for
    portable software.

## Next recommended tranche (remaining P1)

- Pipe policy modernization:
  - raise pipe capacity while preserving POSIX `PIPE_BUF` atomicity semantics.
- Exec argument policy modernization:
  - reconcile `MAXARG` (count-oriented) and `ARG_MAX` (byte-oriented) behavior.
- Keep each limit change paired with explicit kernel memory-accounting rationale
  and regression checks.

## P1 progress (exec argument policy)

Status: Landed (2026-04-05), build-clean.

P1-C replaces pure count-only exec argument gating with explicit dual limits.

### P1-C changes

- Added explicit exec policy constants:
  - `EXEC_ARGC_MAX = 128`
  - `EXEC_ARG_BYTES_MAX = 4096` (aligned with `ARG_MAX`)
- Kept `MAXARG` as a compatibility alias (`MAXARG -> EXEC_ARGC_MAX`) so
  existing callers/tests continue to compile.
- `exec_internal()` now enforces both limits:
  - hard argv-entry cap (`argc < EXEC_ARGC_MAX`)
  - hard total argument-bytes cap (`sum(strlen(arg)+1) <= EXEC_ARG_BYTES_MAX`)
- `sys_exec()` staging array now uses `EXEC_ARGC_MAX` directly.
- Moved exec argv pointer staging (`ustack`) from fixed kernel stack array to a
  one-page `kalloc()` buffer, with a compile-time one-page-fit invariant.

### Why this matters

- Count-only limits underutilize available argument bytes and diverge from
  user-visible `ARG_MAX` expectations.
- Byte-only limits without a count guard can still create pathological argv
  fan-out.
- Heap-backed pointer staging keeps kernel stack growth bounded as argument
  policy evolves.
