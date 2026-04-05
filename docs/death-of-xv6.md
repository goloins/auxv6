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

## What this tranche does NOT claim

- It does not complete userspace 64-bit seek ABI parity.
- It does not attempt to modernize all historical scheduler/process-table design
  choices in one step.
- It does not claim every backend now supports arbitrarily large files; it
  removes silent truncation and improves type correctness in shared core paths.

## Next recommended tranche (P1)

- Raise low xv6-era fixed limits with modern defaults:
  - pipe buffer sizing policy
  - per-process and global file descriptor ceilings
  - argument-vector limits and `OPEN_MAX` consistency
- Keep each limit change paired with explicit kernel memory-accounting rationale
  and regression checks.
