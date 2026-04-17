# Stat Standardization Plan

Date: 2026-04-14
Status: libc + userland source migration completed, runtime validation pending
Owner: libc boundary migration

## Goal

Move userspace to a POSIX-facing `struct stat` ABI while keeping kernel and filesystem drivers on the existing internal/kernel `struct stat` shape.

This keeps the ABI boundary where it belongs: libc.

## Problem Statement

Bash source path fails with:

- `-bash: /etc/profile: file is too large`

Root cause path:

1. Bash loads files via `open` + `fstat` in `ports/bash-5.2.37/builtins/evalfile.c`.
2. It errors if `file_size != finfo.st_size` after cast to `size_t`.
3. Bash includes `<sys/stat.h>` (POSIX struct expectations).
4. libc `stat/fstat/lstat` wrappers currently include `stat.h` (auxv6 internal layout) and pass raw syscall data through.
5. Kernel copies out raw `struct stat` bytes in syscall handlers.

Result: userspace consumers built against `<sys/stat.h>` can read wrong offsets/fields.

## Architecture Decision

- Kernel and drivers remain unchanged and continue using internal layout.
- libc becomes translation boundary:
  - syscall receives internal kstat layout
  - libc maps to POSIX `struct stat`
  - `stat/fstat/lstat` return POSIX layout only

## Work Done In This Session

- Investigated bash file loading code and exact failure condition in `evalfile.c`.
- Verified syscall path in kernel (`sys_fstat`, `sys_stat`, raw copyout).
- Identified mixed-header usage:
  - ports consume `<sys/stat.h>`
  - libc/user programs still largely consume `"stat.h"`
- Initial attempted struct reorder was reverted due cross-filesystem risk and incomplete landing-zone coverage.

## Implemented In This Pass

Boundary translation now lives in libc and kernel/drivers were left untouched.

Completed changes:

1. `libc/posix_fs.c` now includes `sys/stat.h` for public ABI.
2. Added internal `auxv6_kstat` (kernel layout mirror) in `libc/posix_fs.c`.
3. `__auxv6_sys_stat/__auxv6_sys_fstat/__auxv6_sys_lstat` calls now fill `auxv6_kstat`.
4. Added conversion path `auxv6_kstat -> POSIX struct stat` before returning to callers.
5. Replaced auxv6-only file-type checks in this file with POSIX-mode semantics.
6. Updated libc internal consumers to POSIX checks/macros:
  - `libc/tty.c`
  - `libc/path.c`
  - `libc/fts.c`
  - `libc/ftw.c`
  - `libc/glob.c`

Net effect: ports and POSIX-oriented userspace (including bash) consume a POSIX-facing `stat/fstat/lstat` contract via libc.

## Implemented In Follow-up Pass

1. Converted user/libc source includes from `"stat.h"` to `"sys/stat.h"`.
2. Rewrote userland aux-field checks:
  - `st_type` -> `S_IS*` mode checks
  - `st_major/st_minor` -> `major(st_rdev)/minor(st_rdev)`
3. Added compatibility aliases in `sys/stat.h` (`M_*` -> `S_*`) to keep existing mode-bit call sites building during cleanup.
4. Cleaned remaining diagnostics that referenced removed aux fields.

Current scan results after conversion:

- `#include "stat.h"` in `user/**/*.c`: 0
- `#include "stat.h"` in `libc/**/*.c`: 0
- `st_type|st_major|st_minor` in `user/**/*.c`: 0

## Audit: Programs Migrated

The previously identified aux-field users were migrated to POSIX-mode checks/macros and/or `st_rdev` decoding.

Also impacted in libc internals:

- `libc/posix_fs.c`
- `libc/tty.c`
- `libc/ftw.c`
- `libc/fts.c`
- `libc/path.c`
- `libc/glob.c`

## Broader Include Audit

The large include migration has been applied; user/libc sources no longer include `"stat.h"` directly.

Validation order recommendation:

1. ports shell startup validation (`bash`, `dash`).
2. filesystem utility smoke tests (`ls`, `cp`, `mv`, `rm`, `find`, `tar`).
3. device-node tooling checks (`devman`, `mount`, `losetup`, regress suites).

## Risk Notes

- Do not mutate kernel/driver structs for this migration.
- Keep syscall payload definition stable until libc translation is proven.
- Regression hotspots:
  - shell startup (`/etc/profile`, `~/.bashrc` sourcing)
  - directory detection (`ls`, `cp`, `mv`, `rm`, `find`)
  - device-node behavior (`devman`, `mount`, regress suites)

## Regressions:
- dash seems to fail due to hacks for our old stat layout.
    everything else seems to work fine. That needs to be
    fixed at some point, adding TODO/FIXME here
