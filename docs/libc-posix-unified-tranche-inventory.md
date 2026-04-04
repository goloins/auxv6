# Unified Libc/POSIX Tranche Inventory

Date: 2026-04-03

## Purpose

This document is the working inventory for the unified libc/POSIX portability
tranche that establishes the baseline ahead of real thread runtime support.

It is intentionally narrower than "all of POSIX" and more concrete than the
high-level roadmap. The goal is to record what auxv6 already has, what is
missing, what is currently only stubbed or partially truthful, and a sane
implementation order that keeps the public surface coherent.

This tranche does not itself implement pthread/runtime thread enablement, but
it is part of the path to real thread support rather than a declaration that
threads are out of scope.

## Target Band

- Primary target: shell/coreutils/findutils-grade POSIX software.
- Locale model: C locale only.
- Runtime model: static linking, no dynamic loader requirement.
- Portability rule: public headers should not promise more than auxv6 really
  implements.

## Inventory Summary

### Already Landed And Usable Foundations

- Canonical public headers landed for:
  - `limits.h`
  - `inttypes.h`
  - `setjmp.h`
  - `sys/resource.h`
  - `time.h`
  - `sys/time.h`
  - `stdio.h`
  - `unistd.h`
- Tranche-1-style helpers already in-tree:
  - `system`
  - `realpath`
  - `mktemp`, `mkstemp`, `mkostemp`, `mkdtemp`
  - `getlogin`, `getlogin_r`
  - `pause`, `usleep`
  - `confstr`, `pathconf`, `fpathconf`, `sysconf`
  - `posix_openpt`, `grantpt`, `unlockpt`, `ptsname_r`, `openpty`
- Tranche-2-style time/stdio foundations already in-tree:
  - `gettimeofday`, `clock_gettime`, `clock_getres`, `clock_nanosleep`,
    `nanosleep`, `time`, `gmtime`/`localtime`, `mktime`, `strftime`
  - `fseek`, `fseeko`, `ftell`, `ftello`, `fgetpos`, `fsetpos`, `rewind`
  - `vsscanf`, `sscanf`
  - `setvbuf`, `setbuf`, `setlinebuf`, `fflush`
  - `fmemopen` for read-oriented memory streams

### Unified-Tranche Gaps By Class

#### 1. Missing Public Headers

These headers are not present in `include/` yet and should be added only with
truthful declarations:

- `pwd.h`
- `grp.h`

These remain outside the immediate target band and should not be added unless
their surface becomes truthful enough to support real callers:

- `wchar.h`
- `wctype.h`
- `pthread.h`

#### 2. Missing libc/POSIX APIs In The Unified Tranche

The following APIs are not currently exposed or implemented:

- Identity/account database surfaces:
  - `getpwnam`, `getpwuid`
  - `getgrnam`, `getgrgid`
- stdio follow-on gaps already acknowledged by roadmap work:
  - `fscanf`, `vfscanf`
  - `tmpfile`

#### 3. Declared But Not Yet Truthful

These items already appear in public headers or compatibility shims but still
need real semantics before the tranche can be called complete.

- `fmemopen`
  - Current state: present and usable for read-oriented memory streams, but
    not yet a truthful writable POSIX-style implementation.
  - Evidence: `user/stdio.c` rejects non-readable/non-writable buffering paths
    with `ENOSYS`, and the memory stream structure is currently read-biased.
- `clock_settime`
  - Current state: declared, currently `ENOSYS`.
  - Note: this is outside the core unified tranche unless time truthfulness is
    expanded beyond the current target.

#### 4. Compatibility Shims That Should Be Revisited For Truthfulness

These are not all in the unified tranche scope, but they matter because they
affect how truthful the public POSIX surface is:

- `include/posix/sys/stat.h`
  - `umask()` is still an inline stub returning a fixed mode.
- `include/posix/sys/times.h`
  - `times()` is a zero-returning stub for link compatibility.
- `include/sys/types.h`
  - `pthread_*` types are placeholder typedefs only, not thread support.
- `user/posix_fs.c`
  - `faccessat()` only supports `AT_FDCWD`; other directory-fd semantics are
    still `ENOSYS`.
- `user/conf.c`
  - `sethostname()` and `chroot()` currently return `ENOSYS`.

These items should be either:

- left clearly out-of-scope for this tranche and documented as such, or
- tightened so they do not over-promise beyond the target portability band.

## Current Layered Status

### Header Layer

Good shape:

- Canonical `unistd.h`, `stdio.h`, `time.h`, `limits.h`, `inttypes.h`,
  `setjmp.h`, and `sys/resource.h` exist.
- `dirent.h` provides `DIR`, `opendir`, `readdir`, `closedir`, and
  `rewinddir`.

Still missing or incomplete for the unified tranche:

- `pwd.h` and `grp.h` are now in-tree with `/etc`-backed lookup support.
- `stdio.h` now exposes `fscanf`, `vfscanf`, and `tmpfile`.

### Libc Runtime Layer

Good shape:

- Path/tempfile/configuration helpers are split cleanly into focused files.
- Time and stdio ownership is substantially cleaner than before.
- `sscanf`/`vsscanf` live in one place.

Still missing or incomplete for the unified tranche:

- No passwd/group database runtime exists.
- `fscanf`/`vfscanf` now share the same scan engine as `sscanf`/`vsscanf`.
- `tmpfile` now rides the existing tempfile infrastructure.
- `fmemopen` writable semantics are still incomplete.

### Syscall / Kernel Truthfulness Layer

Good shape:

- `lseek`, `dup2`, `fcntl`, `sendto`, `recvfrom`, `setsockopt`, and
  `getsockopt` all landed recently enough that the libc side has more real
  backing than earlier auxv6 snapshots.

Still missing or incomplete for the unified tranche:

- Broader non-tty `ioctl` coverage remains incomplete, though that is larger
  than this tranche by itself.

## Recommended Implementation Order

### Step 1. Header Truthfulness And Small Syscall Backing

Land first because it removes known lies without introducing a large runtime.

- Back `getrlimit` and `setrlimit` with real kernel/libc behavior.
- Add `netdb.h` only with declarations auxv6 can actually support.
- Audit the current public headers for any remaining declarations in the
  target band that still compile but do not behave truthfully.

### Step 1 Status (2026-04-03, tranche start)

- `getrlimit` / `setrlimit` are now moved off inline libc stubs and onto real
  kernel-backed syscalls.
- Current truthful support is intentionally narrow:
  - `RLIMIT_NOFILE`: real per-process soft/hard limit, enforced for fd
    allocation paths including `open`-style allocation, `pipe`, `dup2`, and
    `fcntl(F_DUPFD*)`
  - `RLIMIT_STACK`: truthful fixed-size reporting matching the current exec
    stack model
  - other resource classes may be reported read-only or rejected, but they are
    no longer masquerading as userland-only inline stubs
- Minimal truthful `netdb.h` support is now in-tree for IPv4 host database
  lookups (`gethostbyname`, `gethostbyaddr`, `hstrerror`) backed by the
  existing resolver path.
- `getaddrinfo`/`freeaddrinfo`/`getnameinfo` remain intentionally undeclared
  until their backing is ready.

### Step 2. Shell/Text/Find APIs

Land next because these unlock the widest set of ports.

- `fnmatch`
- `glob` / `globfree`
- `scandir` / `alphasort`
- `nftw` and `fts`
- minimal `locale.h` with explicit C-locale-only semantics

### Step 2 Status (2026-04-03, substantially landed)

- `fnmatch` landed with a truthful minimal wildcard surface (`*`, `?`,
  bracket classes, escape handling, pathname/period/casefold flags).
- `scandir` and `alphasort` landed on top of the existing `opendir`/`readdir`
  path.
- `glob`/`globfree` landed as a minimal truthful expansion layer backed by
  `fnmatch` and `dirent`.
- Both tree-walk APIs are now in-tree for compatibility coverage:
  - `nftw`
  - `fts` family (`fts_open`, `fts_read`, `fts_children`, `fts_close`)
- Userland verification utilities are now in-tree:
  - `_ftwtest`
  - `_nftwtest`
  - `_ftstest`
- Minimal C-locale `locale.h` support is now in-tree via `setlocale` and
  `localeconv`.
- The removal semantics around the new tree-walk tests are now fixed at the
  root:
  - `unlink` no longer removes directories
  - `rmdir` has a real syscall-backed path
  - test setup no longer depends on `-v`
- Remaining Step 2 work in this tranche:
  - hardening and compatibility polish for the new tree-walk implementations
  - keep an eye out for any userland still assuming legacy directory-unlink
    behavior and correct it when found
  - locale follow-on APIs only if a real caller requires them

### Step 3. Identity And Account Lookup

Land after the shell/text/find batch so the user/account model is introduced in
one coherent pass.

- `pwd.h` / `grp.h`
- `getpwnam`, `getpwuid`, `getgrnam`, `getgrgid`
- canonical `/etc/passwd` and `/etc/group` parsing in focused libc runtime code

### Step 3 Status (2026-04-03, landed)

- Canonical headers landed: `pwd.h`, `grp.h`.
- Runtime landed: `/etc/passwd` and `/etc/group` parsing with static-entry
  return model and compatibility fallback for older `/etc/groups` images.
- Consumer wiring landed across the existing passwd/group users so the tree no
  longer grows more ad hoc account parsers.

### Step 4. stdio Follow-On Completion Inside The Same Tranche

Close the lingering stdio portability gaps before calling the tranche done.

- `fscanf`, `vfscanf`
- `tmpfile`
- writable `fmemopen` semantics, if still required by the target ports

### Step 4 Status (2026-04-03, landed)

- `fscanf`/`vfscanf` landed by extending the existing scan engine in
  `user/stdio.c` instead of creating a parallel parser.
- `tmpfile` landed using the existing tempfile/path libc infrastructure.
- Writable `fmemopen` remains intentionally scoped to caller-proven need.

## Recommended Code Organization

Keep the current post-split style. Do not regrow `user/posix.c` into a catchall.

Likely buckets:

- `user/fnmatch.c`
- `user/glob.c`
- `user/scandir.c`
- `user/fts.c` or `user/nftw.c`
- `user/locale.c`
- `user/pwdgrp.c` or separate `user/pwd.c` / `user/grp.c`
- syscall/resource backing split between kernel syscall layer and a small libc
  wrapper file if needed
- keep stdio follow-on work in `user/stdio.c` unless it becomes large enough to
  justify a scan-specific split

## Exit Criteria

The unified tranche is done when all of the following hold:

- The target-band libc/POSIX headers are present and truthful.
- The key shell/find/coreutils-grade ports no longer need local auxv6 shims for
  the APIs listed in this document.
- Any remaining undeclared or stubbed surface outside the tranche is either:
  - deliberately absent,
  - clearly documented as unsupported, or
  - isolated in compatibility shims that do not pretend to be complete.

## Notes For Implementation Sessions

- Prefer fixing the public-surface truthfulness problem at the root rather than
  adding another wrapper that compiles but lies.
- Keep the implementation unixy: small focused translation units, truthful
  headers, and straightforward `/etc`-backed identity lookups instead of
  speculative larger frameworks.
- Threading remains a separate track. Do not let placeholder `pthread_*` types
  blur that boundary.