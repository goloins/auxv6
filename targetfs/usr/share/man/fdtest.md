# fdtest(1)

## Name
fdtest - file-descriptor lifecycle regression suite

## Synopsis
```
fdtest [-v]
```

## Description
`fdtest` is a self-contained in-guest regression suite for the per-process
dynamic file-descriptor table (`struct fdtable`) introduced during the Phase 1A
kernel FD migration.  It exercises the full lifecycle of file descriptors from
open through close, duplication, inheritance across `fork`, I/O multiplexing
via `select` and `poll`, and the `RLIMIT_NOFILE` soft/hard ceiling.

Each sub-test prints a `[PASS]` or `[FAIL]` line; a summary line is printed at
exit.  The process exits 0 if all tests pass, 1 otherwise.

## Options
- `-v` — Verbose: print each sub-test name to stdout as it starts, in addition
  to the normal `[PASS]`/`[FAIL]` output.

## Tests

| # | Name | What is verified |
|---|------|-----------------|
| 1 | `open_close` | Basic open/close cycle on a regular file |
| 2 | `fd_reuse` | POSIX lowest-available slot reuse after `close` |
| 3 | `dup` | `dup(2)` yields a distinct fd sharing the same file |
| 4 | `dup2` | `dup2(2)` places a duplicate at a specific slot |
| 5 | `fcntl_dupfd` | `fcntl(F_DUPFD)` returns a fd at or above the given floor |
| 6 | `fcntl_dupfd_cloexec` | `fcntl(F_DUPFD_CLOEXEC)` sets `FD_CLOEXEC` on the new fd |
| 7 | `fork_inherit` | Child inherits open fds; both sides close independently |
| 8 | `pipe` | `pipe(2)` end-to-end write then read |
| 9 | `select` | `select(2)` detects readability on a pipe with data |
| 10 | `poll` | `poll(2)` detects `POLLIN` on a pipe with data |
| 11 | `rlimit` | `getrlimit`/`setrlimit(RLIMIT_NOFILE)` soft-lower and hard-ceiling checks |
| 12 | `hwm_open` | Open 64 fds simultaneously (well within `NOFILE_DEFAULT`) |
| 13 | `fdtable_expand` | Open 48 fds, exceeding the initial capacity of 32, triggering a table grow |
| 14 | `lseek` | `lseek(2)` with `SEEK_SET`, `SEEK_CUR`, and `SEEK_END` on a regular file |
| 15 | `lseek64` | `_llseek(2)` 5-arg 64-bit seek and `lseek64()` wrapper |
| 16 | `cloexec_exec` | `FD_CLOEXEC` flag survives `fork`, visible in child before exec |

## Notes
- Creates and removes `/tmp/fdtest.tmp` during the run.
- Tests that open many fds assume `/tmp` is a writable tmpfs.
- The `rlimit` test requires `getrlimit`/`setrlimit` to be wired through the
  kernel (`sys_getrlimit`/`sys_setrlimit`).  On a kernel without these
  syscalls the test will report `[FAIL] rlimit: getrlimit`.

## Examples
```
fdtest
fdtest -v
```

## See Also
fcntl(2), dup(2), dup2(2), pipe(2), select(2), poll(2), getrlimit(2), lseek(2), _llseek(2)

## Source Audit
- Source file: user/fdtest.c
- Last updated: 2026-04-05
