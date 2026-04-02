# libc Reorganization Phase 6

This phase shifts the libc work from ABI cleanup into portability work.
Phase 5 removed the native `printf`/`exit` compatibility bridge and landed a
real `_start` path; phase 6 defines the target userland and the next tranches
needed to make auxv6 materially easier to port software onto.

Phase 5 results are documented in `docs/libc-reorg-phase5.md`.

## Target Profile

- Primary target: shell/coreutils/findutils-grade POSIX userland.
- Locale model: C locale only.
- Runtime model: static linking, no dynamic loader requirement.
- Portability rule: headers should only promise APIs that exist or are
  intentionally stubbed and documented.
- Threading is an explicit requirement, but it is tracked as a parallel
  kernel-plus-libc enablement stream rather than folded into the first
  pure-userspace tranche.

## Explicit Non-Goals

- Full POSIX coverage.
- Full locale, message catalog, or NSS behavior.
- Full wide-character-first userspace.
- Binary compatibility with musl or glibc.

## Why Phase 6 Exists

- The next blocker for ports is no longer auxv6's legacy user ABI.
- The current gaps are now mostly about standard-surface completeness,
  correctness, and truthfulness.
- Several standard headers still exist only as compatibility wrappers, and
  some common libc declarations are present without a real implementation.
- `sys/time.h` still carries a zero-valued `gettimeofday()` stub, which is a
  concrete example of a surface that compiles but does not yet behave like a
  useful libc API.

## Current Gap Classes

- Canonical header gaps:
  - top-level `limits.h`, `inttypes.h`, `setjmp.h`, and `sys/resource.h`
    should exist as normal public headers instead of only through
    compatibility paths.
  - top-level `time.h`, `locale.h`, `fnmatch.h`, `glob.h`, `pwd.h`, `grp.h`,
    `wchar.h`, and `wctype.h` are still absent.
- Declared-but-missing or thinly implemented APIs:
  - `system`, `realpath`, `mktemp`-family helpers, `getlogin`/`getlogin_r`,
    `pause`, `confstr`, `pathconf`, and `fpathconf` are the highest-value
    gaps in the current shell-and-utility target band.
- Stubbed semantics:
  - `gettimeofday()` currently returns zeroes.
- Threading:
  - `pthread_*` types exist only as placeholders in `include/sys/types.h`.

## Tranche 1

### Name

Truthful Surface And Low-Kernel-Dependency Portability

### Goals

- Improve porting success without changing the kernel ABI.
- Make canonical public headers match what auxv6 actually implements.
- Land the highest-value userspace-only helpers used by shells and small POSIX
  utilities.

### Planned Deliverables

- Promote existing compatibility-only headers into canonical top-level public
  headers:
  - `limits.h`
  - `inttypes.h`
  - `setjmp.h`
  - `sys/resource.h`
- Audit `unistd.h`, `stdlib.h`, and adjacent public headers so declarations are
  either implemented, explicitly documented as stubs, or removed until they are
  real.
- Implement userspace-only portability helpers with high payoff:
  - `system`
  - `realpath`
  - `mktemp`, `mkstemp`, `mkostemp`, `mkdtemp`
  - `getlogin`, `getlogin_r`
  - `pause`
  - `confstr`, `pathconf`, `fpathconf`
  - `posix_openpt`, `grantpt`, `unlockpt`
- Keep the implementation split clean instead of growing `user/posix.c` back
  into a monolith. Likely buckets:
  - `user/conf.c`
  - `user/tempfile.c`
  - `user/path.c`
  - minimal POSIX PTY additions in `user/tty.c`
  - only small process-wrapper additions in `user/posix.c` if unavoidable

### Initial Landing Status (2026-04-02)

- Canonical top-level public headers promoted:
  - `limits.h`
  - `inttypes.h`
  - `setjmp.h`
  - `sys/resource.h`
- The corresponding `include/posix/*` headers now forward to the canonical
  top-level headers instead of carrying their own divergent copies.
- New tranche-1 runtime buckets landed:
  - `user/conf.c` for `sysconf`, `pathconf`, `fpathconf`, `confstr`,
    `getlogin`, `getlogin_r`, `usleep`, `pause`, hostname helpers, and small
    synchronization stubs.
  - `user/path.c` for `realpath`.
  - `user/tempfile.c` for `mktemp`, `mkstemp`, `mkostemp`, and `mkdtemp`.
- Existing runtime files were extended rather than re-monolithized:
  - `user/posix.c` now provides `execl`, `execlp`, and `system`.
  - `user/tty.c` now provides `posix_openpt`, `grantpt`, and `unlockpt`.
  - `user/stdlib.c` picked up low-cost truthfulness helpers for the random,
    division, and single-byte multibyte conversion families.
- Port fallout from the libc split was fixed in the dash port by teaching
  `ports/dash-0.5.12/Makefile.auxv6` to link the new runtime objects.

### Tranche 1 Non-Goals

- Real time and date semantics.
- `time.h`, `clock_gettime`, `gettimeofday` correctness, or `strftime`.
- `fnmatch`, `glob`, `scandir`, or tree-walk APIs such as `nftw` or `fts`.
- `pwd.h` or `grp.h` lookups.
- Locale beyond the C locale.
- Pthread runtime support.

### Validation

- Compile probes that include only canonical public headers.
- Rebuild the dash port and representative in-tree userland.
- Add focused probes for `system`, `realpath`, `mkstemp`, `getlogin_r`, and
  `pathconf`.
- Preserve current `qemu-oldinit` and `qemu-nox` smoke viability.

### Initial Validation Status (2026-04-02)

- `sudo make user/conf.o user/path.o user/tempfile.o user/tty.o user/posix.o user/stdlib.o aux.kern` passed after fixing one missing prototype include in `user/posix.c`.
- A canonical-header compile probe covering `posix_openpt`, `grantpt`,
  `unlockpt`, `pathconf`, `confstr`, `getlogin_r`, `usleep`, `pause`,
  `system`, `realpath`, `mkstemp`, `execl`, `execlp`, `setjmp`, and
  `getrlimit` passed under the cross compiler with `-Werror`.
- `sudo make -f ports/dash-0.5.12/Makefile.auxv6 clean all` passed after
  updating the port's runtime object list to include `user/conf.o`,
  `user/path.o`, and `user/tempfile.o`.
- The dash rebuild still emits the pre-existing `signames.c` excess-initializer
  warnings, but the tranche-1 work did not introduce a new dash build failure.

### Exit Criteria

- The top-level header set reflects the real tranche-1 public surface.
- No tranche-1 declaration remains purely aspirational.
- Dash plus representative shell/coreutils-style probes build without local
  auxv6 shims for the tranche-1 APIs.

## Later Tranches

### Tranche 2

Time And Stream Correctness

- Add canonical `time.h`.
- Replace the zero-valued time stubs with real time APIs:
  - `time`
  - `gettimeofday`
  - `clock_gettime`
  - `nanosleep` or `usleep`
  - `gmtime_r`, `localtime_r`, `mktime`, `strftime`
- Finish the high-value stdio positioning and scanning surfaces:
  - `fseek`, `ftell`, `fseeko`, `ftello`
  - `rewind`, `fgetpos`, `fsetpos`, `setvbuf`
  - `sscanf`, `vsscanf`

### Tranche 3

Shell, Text, And Find Surfaces

- Add `fnmatch.h` and `fnmatch`.
- Add `glob.h`, `glob`, and `globfree`.
- Add `scandir` and `alphasort`.
- Pick and implement one directory-tree-walk surface for findutils-grade ports:
  - `nftw`, or
  - `fts`
- Add minimal `locale.h` with C-locale-only behavior.

### Tranche 4

Identity And Porting Polish

- Add `pwd.h` and `grp.h`.
- Implement `getpwnam`, `getpwuid`, `getgrnam`, and `getgrgid`.
- Clean up remaining portability miscellany around resource, hostname, and
  process-environment APIs.
- Prune or gate any declarations that still do not meet the target profile.

## Parallel Thread-Enablement Track

The target profile now explicitly includes threads, but this is not a libc-only
tranche.

### Pre-Req Cleanup

- Stop treating the placeholder `pthread_*` typedefs in `include/sys/types.h`
  as real thread support.
- Make libc state TLS-ready where needed, especially `errno` and other
  reentrancy-sensitive global or static state.

### Kernel Work

- Choose a shared-address-space thread model.
- Define thread creation, exit, join, and TID semantics.
- Define signal, wait, and scheduler behavior for multi-threaded processes.

### Libc Work

- Add a real `pthread.h` only once the runtime exists.
- Target a minimal useful subset first:
  - `pthread_create`
  - `pthread_join`
  - `pthread_self`
  - `pthread_equal`
  - `pthread_once`
- Add mutexes and condition variables before larger pthread surfaces such as
  rwlocks or thread-specific data.

### Why It Stays Parallel To Tranche 1

- Tranche 1 is intentionally userspace-only and header-focused.
- Threads require coordinated kernel, libc, signal, and runtime work.
- Keeping the thread track separate prevents small portability wins from being
  blocked on a much larger design effort.