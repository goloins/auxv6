# libc Reorganization Phase 1

This tranche starts the libc cleanup without breaking the current auxv6 userland ABI.

Phase 2 continuation is documented in `docs/libc-reorg-phase2.md`.

## Goals

- Make canonical top-level standard headers the preferred include surface.
- Push the legacy auxv6 catch-all user API behind an explicit native header.
- Preserve existing builds by keeping compatibility shims in place.
- Prepare the tree for later ABI cleanup around `printf`, `exit`, `getcwd`, and startup code.

## Landed In Phase 1

- Added canonical top-level headers for `stdarg.h`, `ctype.h`, `dirent.h`, and `sys/ioctl.h`.
- Converted the old `include/posix/*` copies of those headers into thin wrappers.
- Introduced `include/auxv6/user.h` as the native auxv6 userland ABI header.
- Reduced `include/user.h` to a compatibility shim so new code can stop treating it as the only public API surface.
- Split the Makefile userland runtime object list into `LIBC_OBJS` and `LIBAUXV6_OBJS` while keeping the final link behavior unchanged.
- Normalized the first wave of libc source files and tty utilities to canonical include paths.
- Moved `stpcpy()` out of `user/posix.c` and into `user/ulib.c` so core string routines keep drifting out of the POSIX shim bucket.
- Split the first real implementation chunks out of the legacy monoliths: `user/fmt.c`, `user/dirent.c`, `user/tty.c`, and `user/inet.c` now carry code that previously lived inside `user/posix.c` and `user/ulib.c`.

## Deliberately Deferred

- Renaming the auxv6 fd-based `printf` API.
- Resolving the `exit` and `getcwd` namespace conflicts at the ABI level.
- Splitting the flat ULIB link into real archives such as `libc.a` and `libauxv6.a`.
- Adding a user-space crt entry path (`_start`, argc/argv/envp setup, `exit(main(...))`).

## Next Recommended Steps

1. Move more standard string and stdlib routines out of `user/posix.c` into `user/ulib.c` or subsystem-specific files.
2. Add `crt0` and stop linking user binaries directly at `main`.
3. Rename the native fd-based `printf` surface and stop exporting it as the public `printf` name.
4. Split libc sources by subsystem: string, stdlib, dirent, stdio, process, tty, and resolver.