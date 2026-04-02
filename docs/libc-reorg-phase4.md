# libc Reorganization Phase 4

This tranche reconciles the native auxv6 user ABI header with the canonical
standard and POSIX headers so implementation files can include both surfaces
without local forward declarations or fragile include-order rules.

## Goals

- Remove the remaining hard declaration conflicts between `auxv6/user.h` and
  canonical headers such as `string.h`, `stdlib.h`, and `unistd.h`.
- Normalize shared prototype types to the canonical public spellings where the
  auxv6 ABI already matches in practice.
- Eliminate the phase 3 local `strlcpy` forward-declaration workaround in
  `user/errstr.c`.
- Preserve the current userland ABI where it is still intentionally nonstandard
  (`exit`, fd-based `printf`, and native `getcwd` behavior).

## Landed In Phase 4

- Updated `include/auxv6/user.h` to include `sys/types.h` and use canonical
  shared types for many existing interfaces, including `pid_t`, `id_t`,
  `uid_t`, `gid_t`, `mode_t`, `off_t`, `ssize_t`, `size_t`, and `intptr_t`.
- Harmonized native declarations for process, file, tty, memory, and socket
  entry points where the ABI already lined up, including `fork`, `wait*`,
  `read`, `write`, `readlink`, `lseek`, `chmod`, `chown`, `getpid`, `getuid`,
  `sbrk`, `sleep`, `alarm`, `send*`, `recv*`, `memmove`, `strchr`, `strncmp`,
  `strlen`, `memset`, and `malloc`.
- Guarded the auxv6-native declarations that still intentionally differ from
  the standard surface so they do not collide with canonical headers in mixed
  include sets, notably `exit`, `getcwd`, `tcsetpgrp`, `tcgetpgrp`, and the
  native fd-based `printf`.
- Added a shared auxv6 `exit(...)` compatibility path across
  `include/auxv6/user.h` and `include/stdlib.h` using a stable alias plus macro
  rewrite, so native `exit()` call sites survive both include orders.
- Updated `user/ulib.c` and `user/umalloc.c` so their function signatures match
  the harmonized declarations.
- Removed the temporary phase 3 workaround in `user/errstr.c`; it now includes
  canonical `errno.h` and `string.h` directly and uses the standard
  `strerror_r(..., size_t)` signature.

## Validation

- Header coexistence was checked directly with syntax-only compiler probes for:

```c
#include "auxv6/user.h"
#include "string.h"
```

```c
#include "auxv6/user.h"
#include "stdlib.h"
```

```c
#include "auxv6/user.h"
#include "unistd.h"
```

- Reverse include order was also checked for canonical headers before
  `auxv6/user.h`, including `stdio.h`, `unistd.h`, and `stdlib.h` with live
  `exit()` and `exit(1)` call sites.
- `make aux.kern` completed successfully through the normal workspace task
  path.
- A scratch replay of the userland compile and link rules, using the same
  Makefile flags but writing outputs under `/tmp`, succeeded for representative
  programs: `lsof`, `which`, `file`, `sh`, `ls`, `cat`, `echo`, `grep`, `mv`,
  `rm`, and `wc`.
- Normal staged user-target rebuilds in this workspace remain vulnerable to the
  existing root-owned generated-artifact issue (`*.sym` files), so scratch-path
  validation was used to avoid reporting a false regression in this tranche.

## Resulting File Roles

- `include/auxv6/user.h`: auxv6-native ABI surface with canonical type spellings
  where possible and guarded nonstandard declarations where necessary.
- `include/stdlib.h`: standard library surface plus the shared auxv6
  `exit(...)` compatibility path when the native auxv6 header is active.
- `user/errstr.c`: canonical string-header consumer again; no local libc
  declaration workaround remains.

## Deliberately Deferred

- Renaming the auxv6 fd-based `printf` ABI so standard `printf` no longer needs
  macro-based coexistence.
- Replacing the native `exit(void)` / `getcwd` behavior with a fully standard C
  runtime startup and termination path.
- Splitting the flat user runtime into real archives such as `libc.a` and
  `libauxv6.a`.
- Any large-scale source movement beyond what was required to make the header
  surfaces coexist.

## Next Recommended Steps

1. Decide whether the next tranche should begin true ABI cleanup for `exit`,
   `printf`, and `getcwd`, or keep the current shim-based compatibility layer.
2. If a real libc ABI is now the priority, add crt startup so user binaries stop
   linking directly at `main`.
3. Continue shrinking the remaining monoliths only after the native versus
   canonical header boundary is stable.