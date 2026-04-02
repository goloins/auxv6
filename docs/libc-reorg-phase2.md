# libc Reorganization Phase 2

This tranche continues the libc cleanup by finishing the userland include-path
sweep and carving more coherent modules out of the remaining monoliths.

## Goals

- Eliminate the remaining legacy `../include/...`, `user.h`, and `posix/...`
  include usage from `user/*.c` and `user/*.S`.
- Split standard-library and environment helpers out of `user/ulib.c`.
- Split filesystem and path compatibility wrappers out of `user/posix.c`.
- Move PTY allocation into the tty helper module so terminal code lives in one
  place.
- Keep the current auxv6 ABI intact while reducing source-file sprawl.

## Landed In Phase 2

- Added `user/env.c` for `getenv`, `putenv`, `setenv`, `unsetenv`, and `clearenv`.
- Added `user/stdlib.c` for `atoi`, numeric conversion helpers, integer helpers,
  `atexit`, `abort`, `bsearch`, `qsort`, `rand`, and `srand`.
- Added `user/posix_fs.c` for `__posix_stat`, `__posix_fstat`,
  `__posix_lstat`, `__posix_getcwd`, `faccessat`, `access`, and `open64`.
- Moved `openpty` out of `user/posix.c` and into `user/tty.c`.
- Updated `Makefile` so `LIBC_OBJS` now includes `user/env.o`,
  `user/stdlib.o`, and `user/posix_fs.o`.
- Completed the userland canonical-include sweep: no `user/*.c` or `user/*.S`
  files still include `../include/...`, plain `user.h`, or `posix/...` paths.
- Reduced `user/ulib.c` to string, memory, allocation-adjacent, and error-string
  helpers instead of mixing in environment and general stdlib code.
- Reduced `user/posix.c` to identity, signal, exec, wait, and environment-setup
  wrappers instead of mixing in filesystem and PTY support.

## Validation

- A widened sudo build of representative userland targets completed successfully,
  covering auth/session tools, admin and storage tools, network/socket tools,
  and regression utilities.
- Verified command:

```sh
sudo make user/login user/passwd user/su user/init user/v6init user/telinit user/runlevel user/whoami user/id user/man user/which user/file user/lsof user/devman user/df user/free user/mount user/mounts user/umount user/killall user/lsblk user/ping user/telnet user/netcat user/ifconfig user/route user/netstat user/nslookup user/ip user/arp user/rarp user/netinfo user/sockettest user/tcptest user/udptest user/v6dhcpd user/usertests user/stressfs user/symlinktest user/losetup user/isotest
```

- `sudo make aux.kern` completed successfully after the module split.
- The only warnings observed during the widened userland build were the usual
  linker warnings about RWX LOAD segments.

## Resulting File Roles

- `user/ulib.c`: string, memory, BSD string, duplication, and error-string helpers.
- `user/env.c`: environment mutation and lookup.
- `user/stdlib.c`: numeric parsing, integer helpers, process-abort helpers,
  sorting, and PRNG state.
- `user/tty.c`: tty name helpers, password input, and PTY allocation.
- `user/posix_fs.c`: POSIX stat, cwd, and access compatibility.
- `user/posix.c`: identity, signal, exec, wait, and `environ` bootstrap.

## Deliberately Deferred

- Renaming the auxv6 fd-based `printf` ABI.
- Resolving the `exit` and `getcwd` ABI conflicts with a real C runtime entry path.
- Splitting the flat user runtime into real archives such as `libc.a` and
  `libauxv6.a`.
- Adding crt startup (`_start`, argc/argv/envp setup, `exit(main(...))`).

## Next Recommended Steps

1. Split `user/ulib.c` one more time if desired so string core and error-string
   helpers are no longer mixed.
2. Decide whether `execvp` path-search helpers should stay in `user/posix.c` or
   move into a smaller exec-focused module.
3. Start ABI cleanup only after the library layout is stable enough to avoid
   redoing the same work behind new names.