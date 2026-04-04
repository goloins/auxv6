# Unified Libc/POSIX Tranche Inventory

Date: 2026-04-03

## Purpose

This document is the working inventory for the unified non-thread libc/POSIX
portability tranche.

It is intentionally narrower than "all of POSIX" and more concrete than the
high-level roadmap. The goal is to record what auxv6 already has, what is
missing, what is currently only stubbed or partially truthful, and a sane
implementation order that keeps the public surface coherent.

This tranche does **not** include pthread/runtime thread enablement. That
remains a separate kernel-plus-libc track.

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
  - `sys/resource.h` (header exists, but `getrlimit`/`setrlimit` remain stubs)
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

- `fnmatch.h`
- `glob.h`
- `locale.h`
- `pwd.h`
- `grp.h`
- `netdb.h`

These remain outside the immediate target band and should not be added unless
their surface becomes truthful enough to support real callers:

- `wchar.h`
- `wctype.h`
- `pthread.h`

#### 2. Missing libc/POSIX APIs In The Unified Tranche

The following APIs are not currently exposed or implemented:

- Shell/text/find surfaces:
  - `fnmatch`
  - `glob`, `globfree`
  - `scandir`, `alphasort`
  - one directory-tree-walk API:
    - `nftw`, or
    - `fts`
- Identity/account database surfaces:
  - `getpwnam`, `getpwuid`
  - `getgrnam`, `getgrgid`
- stdio follow-on gaps already acknowledged by roadmap work:
  - `fscanf`, `vfscanf`
  - `tmpfile`

#### 3. Declared But Not Yet Truthful

These items already appear in public headers or compatibility shims but still
need real semantics before the tranche can be called complete.

- `getrlimit`, `setrlimit`
  - Current state: inline stubs in `include/sys/resource.h` that return `-1`.
  - Required outcome: real syscall backing or a narrower truthful contract.
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

- No `fnmatch.h`, `glob.h`, `locale.h`, `pwd.h`, `grp.h`, or `netdb.h`.
- `dirent.h` does not yet expose `scandir` or `alphasort`.
- `stdio.h` does not yet expose `fscanf`, `vfscanf`, or `tmpfile`.

### Libc Runtime Layer

Good shape:

- Path/tempfile/configuration helpers are split cleanly into focused files.
- Time and stdio ownership is substantially cleaner than before.
- `sscanf`/`vsscanf` live in one place.

Still missing or incomplete for the unified tranche:

- Pattern matching and globbing runtime does not exist.
- No find-style tree walk helper exists.
- No passwd/group database runtime exists.
- `fscanf`/`vfscanf` are absent even though `sscanf`/`vsscanf` exist.
- `tmpfile` is absent.
- `fmemopen` writable semantics are still incomplete.

### Syscall / Kernel Truthfulness Layer

Good shape:

- `lseek`, `dup2`, `fcntl`, `sendto`, `recvfrom`, `setsockopt`, and
  `getsockopt` all landed recently enough that the libc side has more real
  backing than earlier auxv6 snapshots.

Still missing or incomplete for the unified tranche:

- `getrlimit` / `setrlimit` need real kernel backing if they remain declared.
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
- one tree-walk API (`nftw` or `fts`)
- minimal `locale.h` with explicit C-locale-only semantics

### Step 2 Status (2026-04-03, in progress)

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
- Remaining Step 2 work in this tranche:
  - hardening and compatibility polish for the new tree-walk implementations
  - file-removal API hardening: `rm` now uses POSIX stat wrappers and directory
    removals route through a real `rmdir` syscall path; raw `unlink` no longer
    removes directories
  - locale follow-on APIs only if a real caller requires them

### Step 3. Identity And Account Lookup

Land after the shell/text/find batch so the user/account model is introduced in
one coherent pass.

- `pwd.h` / `grp.h`
- `getpwnam`, `getpwuid`, `getgrnam`, `getgrgid`
- decide whether `/etc/passwd` and `/etc/groups` parsing lives in new focused
  libc files or a small identity runtime unit

### Step 4. stdio Follow-On Completion Inside The Same Tranche

Close the lingering stdio portability gaps before calling the tranche done.

- `fscanf`, `vfscanf`
- `tmpfile`
- writable `fmemopen` semantics, if still required by the target ports

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