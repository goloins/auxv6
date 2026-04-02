# libc Reorganization Phase 5

This tranche starts the ABI cleanup itself instead of only reconciling header
surfaces. The goal is to make the standard C and POSIX entry points real,
while keeping narrowly scoped source-compatibility shims in the native auxv6
header during the transition.

## Goals

- Stop linking user programs directly at `main` and introduce a real `_start`
  path.
- Convert `exit` from the legacy void/no-status ABI into a standard
  status-carrying termination path.
- Replace the native fd-based `printf` export with `dprintf`, so standard
  `printf` can exist as an actual function instead of a macro veneer.
- Promote `getcwd` to the standard `char *getcwd(char *, size_t)` surface.
- Keep live notes in-tree while the tranche is still being landed.

## Landed So Far

- Added a real user `_start` object in `user/crt0.S` and switched user links to
  enter at `_start` instead of directly at `main`.
- Renamed the raw user syscall wrappers for `exit` and `getcwd` to
  `__auxv6_sys_exit` and `__auxv6_sys_getcwd`, freeing the public names for
  libc wrappers.
- Updated the kernel exit path to carry an explicit status from `sys_exit()`
  through `proc.c`, while still preserving signal-derived wait status when one
  was already recorded.
- Started the libc-facing ABI shift:
  - `user/stdlib.c` now owns the real `exit` symbol and `_Exit` path.
  - `user/posix_fs.c` now owns the standard `getcwd` function.
  - `user/printf.c` now exports `dprintf`/`vdprintf`.
  - `user/stdio.c` now exports real `printf`/`vprintf`.
- Kept temporary native-source compatibility in `include/auxv6/user.h`:
  - `printf` maps to `dprintf` when `stdio.h` is not active.
  - `exit()` and `exit(status)` calls are still accepted in auxv6-native code
    through a variadic compatibility macro that targets the real libc `exit`
    symbol, while `stdlib.h` can undefine that shim.

## Validation So Far

- Direct mixed-header probes passed for:

```c
#include "auxv6/user.h"
int main(void){ exit(); dprintf(2, "x\n"); return 0; }
```

```c
#include "auxv6/user.h"
#include "stdlib.h"
int main(void){ exit(1); }
```

```c
#include "auxv6/user.h"
#include "stdio.h"
int main(void){ printf("x\n"); dprintf(2, "y\n"); return 0; }
```

```c
#include "auxv6/user.h"
#include "unistd.h"
int main(void){ char buf[32]; return getcwd(buf, sizeof(buf)) ? 0 : 1; }
```

- `make aux.kern` completed successfully after the kernel/user exit ABI change.
- The dash-independent fallback boot path also passed: after switching the ISO
  helper to prefer `mkisofs` on this host, `sudo make qemu-oldinit` completed
  successfully and confirmed that the old-init path still boots under the new
  ABI.
- The normal dash-backed boot path now passes again as well. The break was in
  the out-of-tree port steering, not a newly missing libc primitive: the dash
  port makefile was still linking at `main`, still using the pre-phase-5 user
  runtime object subset, and still trying to build auxv6 runtime objects
  through stale local rules. After aligning `ports/dash-0.5.12/Makefile.auxv6`
  with the current `_start` entry and runtime split, `sudo make qemu-nox`
  successfully booted through `/bin/dash /etc/rc.d/rc.S` and reached login.
- Representative user objects compiled successfully across the updated runtime
  and compatibility-heavy programs, including `sh`, `init`, `termcheck`, and
  `usertests`.
- Representative user links under the new `_start` entry passed for `pwd`,
  `echo`, `grep`, `ls`, `file`, `lsof`, `forktest`, `sh`, and `init`.
- Observed linker output was limited to the existing RWX LOAD-segment warnings
  for user binaries.

## In Progress

- Sweep remaining in-tree code that should stop depending on the native
  compatibility macros.
- Update roadmap and repo-memory notes as the tranche progresses.

## Source Sweep Started

- The first post-ABI source-migration cluster has already been converted away
  from the compatibility shims and onto explicit `dprintf` plus `exit(status)`
  calls:
  - `user/pwd.c`
  - `user/echo.c`
  - `user/grep.c`
  - `user/ls.c`
  - `user/file.c`
  - `user/lsof.c`
  - `user/forktest.c`
  - `user/init.c`
- That cluster recompiles cleanly against the new ABI layer.

## Source Sweep Expanded

- Additional command/admin utilities have now been migrated off the native
  compatibility macros and onto explicit `dprintf` plus explicit exit status
  handling:
  - `user/arp.c`
  - `user/cat.c`
  - `user/kill.c`
  - `user/ln.c`
  - `user/mkdir.c`
  - `user/ps.c`
  - `user/mounts.c`
  - `user/telinit.c`
  - `user/which.c`
  - `user/tail.c`
  - `user/lsblk.c`
  - `user/runlevel.c`
  - `user/umount.c`
  - `user/clear.c`
  - `user/chmod.c`
  - `user/id.c`
  - `user/getty.c`
  - `user/free.c`
  - `user/time.c`
  - `user/netstat.c`
  - `user/man.c`
  - `user/wc.c`
  - `user/rarp.c`
  - `user/halt.c`
  - `user/chvt.c`
  - `user/termdemo.c`
  - `user/zombie.c`
  - `user/reset.c`
  - `user/fatregress.c`
- Network utilities, bootstrap paths, and socket self-tests were also moved to
  the explicit ABI surface:
  - `user/v6init.c`
  - `user/nslookup.c`
  - `user/route.c`
  - `user/tcptest.c`
  - `user/sockettest.c`
  - `user/ping.c`
- These follow-on sweep clusters all rebuilt cleanly under the real userland
  link path. Representative validation passed with grouped builds for:
  - command/admin utilities
  - identity/login-adjacent tools
  - legacy old-init and small leaf tools
  - network utilities and socket self-tests

## Remaining Cleanup Boundary

- The real ABI shift is in place, but a smaller set of larger interactive,
  network, and test programs still rely on the native-source compatibility
  macros from `include/auxv6/user.h` instead of calling `dprintf` and
  `exit(status)` directly.
- The highest-signal remaining holdouts are now concentrated in files such as
  `user/termcheck.c`, `user/sh.c`, `user/login.c`, `user/losetup.c`,
  `user/netcat.c`, `user/ip.c`, `user/su.c`, `user/v6dhcpd.c`, and some larger
  regression/self-test sources.
- No `envp` startup handoff was added yet; `_start` now standardizes the
  `main(argc, argv)` entry path, but `environ` still comes from the existing
  userspace bootstrap in `user/posix.c`.
- The next source-cleanup pass can now be mechanical instead of architectural.