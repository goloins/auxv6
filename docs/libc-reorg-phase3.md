# libc Reorganization Phase 3

This tranche continues the libc cleanup by shrinking `user/ulib.c` down to the
small legacy runtime core and moving the remaining extended string surface into
files that match the public headers more directly.

## Goals

- Keep `user/ulib.c` limited to the tiny xv6-era runtime primitives.
- Move extended string and BSD string helpers behind a dedicated implementation
  file instead of leaving them mixed into `ulib.c`.
- Move `errno` storage and string-based error reporting into their own module.
- Preserve the current auxv6 ABI and build flow while improving source-file
  separation.
- Leave compact notes so a future handoff does not need to reconstruct the
  split from diffs.

## Landed In Phase 3

- Added `user/string.c` for extended string, tokenization, BSD string, and
  duplication helpers previously implemented in `user/ulib.c`.
- Added `user/errstr.c` for global `errno` storage plus `strerror`,
  `strerror_r`, and `strsignal`.
- Reduced `user/ulib.c` to the minimal core primitives: `strcpy`, `strcmp`,
  `strncmp`, `strlen`, `memset`, `strchr`, and `memmove`.
- Updated `Makefile` so `LIBC_OBJS` now includes `user/string.o` and
  `user/errstr.o`.
- Resolved one build-time header conflict during the split by keeping
  `user/errstr.c` off the canonical `string.h` include path and forward-
  declaring only `strlcpy`, since `auxv6/user.h` and `string.h` still differ in
  a few prototype spellings such as `memmove` and `strchr`.

## Validation

- Verified representative rebuild command:

```sh
sudo make user/sh user/login user/man user/file user/lsof user/ping user/telnet user/netcat user/usertests _lsof _which _file aux.kern
```

- The representative rebuild completed successfully after the `errstr.c`
  prototype fix.
- The staged tool targets `_lsof`, `_which`, and `_file` were rebuilt as part of
  the same validation command.
- Observed warnings were limited to the usual linker warnings about RWX LOAD
  segments on user binaries.

## Resulting File Roles

- `user/ulib.c`: tiny legacy runtime primitives only.
- `user/string.c`: extended string, tokenization, BSD string, and duplication helpers.
- `user/errstr.c`: `errno` storage and human-readable error and signal strings.
- `user/env.c`: environment lookup and mutation.
- `user/stdlib.c`: numeric conversion, sorting, PRNG, and termination helpers.

## Deliberately Deferred

- Normalizing prototype differences between `auxv6/user.h` and the canonical
  standard headers.
- Any ABI rename of the native fd-based `printf` surface.
- `exit` and `getcwd` cleanup through a real CRT entry path.
- Real archive packaging such as `libc.a` and `libauxv6.a`.

## Next Recommended Steps

1. Reconcile the remaining declaration mismatches between `auxv6/user.h` and
   canonical headers so implementation files do not need narrow local forward
   declarations.
2. Optionally split `user/posix.c` one more time if `execvp` path-search logic
   should live apart from signal and identity wrappers.
3. Start ABI cleanup only after the header surfaces and file layout stop moving.