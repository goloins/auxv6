# `copyin()` Materialization Audit

Date: 2026-04-12

This note audits the next tempting step after explicit `vmreserve()` plus fault-time zerofill and `copyout()`-side materialization:

Should `copyin()` be allowed to materialize absent `VMA_ZEROFILL` pages?

Short answer: not generically.

Generic `copyin()` materialization would silently change the meaning of large parts of the syscall ABI. The problem is not mechanical implementation difficulty. The problem is semantic widening.

## Current State

Today the VM contract is intentionally asymmetric:

- page faults can materialize explicit `VMA_ZEROFILL` reservations
- `copyout()` can now materialize explicit `VMA_ZEROFILL` destinations for syscall output paths
- `copyin()` still requires present source pages

That asymmetry is currently desirable.

## Why `copyin()` Is Different From `copyout()`

When `copyout()` materializes a reserved page, the kernel is writing new data into user memory. Turning an untouched page into a writable zero page and then immediately overwriting bytes is semantically reasonable for explicit reservation buffers.

When `copyin()` materializes a reserved page, the kernel is reading from user memory. If the page was untouched, the kernel would observe zeros that the program never explicitly wrote. That can be correct for a few narrow APIs, but it is dangerous as a generic rule because many syscalls interpret zero bytes as real user intent.

## Surface Classification

### Category A: Absolutely Unsafe For Generic `copyin()` Materialization

These paths would silently reinterpret missing user memory as meaningful zero input.

#### 1. Syscall argument fetch from the user stack

Key sites:

- [kernel/core/syscall.c](kernel/core/syscall.c#L18)
- [kernel/core/syscall.c](kernel/core/syscall.c#L84)
- [kernel/core/syscall.c](kernel/core/syscall.c#L91)

Why unsafe:

- `fetchint()` uses `copyin()` to pull integer syscall arguments from the user stack
- if `copyin()` started materializing absent pages, a missing stack page could become zeros instead of producing an error
- that would mutate malformed or invalid syscall invocations into valid-looking zero-valued arguments

This alone is enough reason not to make generic `copyin()` self-materializing.

#### 2. Path and string parsing

Key sites:

- [kernel/core/syscall.c](kernel/core/syscall.c#L49)
- [kernel/core/sysfile.c](kernel/core/sysfile.c#L47)
- representative consumers in [kernel/core/sysfile.c](kernel/core/sysfile.c#L1141), [kernel/core/sysfile.c](kernel/core/sysfile.c#L2015), and [kernel/core/sysfile.c](kernel/core/sysfile.c#L3056)

Why unsafe:

- string readers copy byte-by-byte until `NUL`
- an untouched zerofill page would look like a valid zero-terminated string immediately
- that means absent memory could be reinterpreted as `""`, truncated paths, truncated argv entries, or truncated mount/device names

That is the worst kind of bug: not a clean failure, but silent reinterpretation.

#### 3. `exec()` argv ingestion

Key sites:

- [kernel/core/sysfile.c](kernel/core/sysfile.c#L2257)
- [kernel/core/sysfile.c](kernel/core/sysfile.c#L2270)

Why unsafe:

- `sys_exec()` fetches argv pointers and then copies each argument string
- generic `copyin()` materialization would allow absent user-stack/input pages to become zeros and alter the executed command line

This is not acceptable for process image setup.

### Category B: Structured Control Inputs With High Semantic Risk

These are not string-termination bugs, but zero-filled control structures could still be misinterpreted as real input.

#### 4. Signal frame restore

Key site:

- [kernel/core/sysproc.c](kernel/core/sysproc.c#L277)

Why unsafe:

- `sigreturn` restores a saved frame from user memory
- treating an absent frame as zeros would effectively synthesize register state
- failure is correct here; zero-materialization would be disastrous

#### 5. Signal and sigmask configuration

Key sites:

- [kernel/core/proc.c](kernel/core/proc.c#L456)
- [kernel/core/proc.c](kernel/core/proc.c#L510)

Why unsafe:

- zero-filled handlers or masks are not “no opinion”; they are actual policy values
- absent memory must not be accepted as a request to reset behavior

#### 6. `ioctl()` structured arguments

Representative sites:

- [kernel/core/sysproc.c](kernel/core/sysproc.c#L839)
- [kernel/core/sysproc.c](kernel/core/sysproc.c#L905)
- [kernel/core/sysproc.c](kernel/core/sysproc.c#L926)
- [kernel/core/sysproc.c](kernel/core/sysproc.c#L968)

Why unsafe:

- many `ioctl`s treat the pointed-to object as in/out state
- zero-filled termios, winsize, audio args, tty control ints, or tun/tap config structs would be interpreted as real caller intent

These should fail on absent input memory, not auto-create it.

#### 7. Socket address and option inputs

Representative sites:

- [kernel/net/socket.c](kernel/net/socket.c#L960)
- [kernel/net/socket.c](kernel/net/socket.c#L1031)
- [kernel/net/socket.c](kernel/net/socket.c#L1324)
- [kernel/net/socket.c](kernel/net/socket.c#L1933)
- [kernel/net/socket.c](kernel/net/socket.c#L1985)

Why unsafe:

- zero-filled `sockaddr_in` or optlen/optval data would be consumed as if supplied intentionally
- this can alter bind/connect/sendto/setsockopt behavior in subtle ways

Again, absent input memory must stay an error.

#### 8. `select()` / `poll()` / timeout control blocks

Representative sites:

- [kernel/core/sysfile.c](kernel/core/sysfile.c#L2517)
- [kernel/core/sysfile.c](kernel/core/sysfile.c#L2558)
- [kernel/core/sysfile.c](kernel/core/sysfile.c#L2377)

Why unsafe:

- zero-filled fd sets mean “monitor nothing”
- zero-filled timeout means “don’t wait” or “wait forever” depending on layout/interpretation
- this would turn missing user memory into a real polling request

### Category C: Raw Payload Inputs That Are Less Dangerous But Still Semantically Significant

These are the cases people usually point to when asking for `copyin()` materialization. They look safer, but they still change application semantics.

Representative sites:

- [kernel/core/sysfile.c](kernel/core/sysfile.c#L1067) for `write()`-style file output
- [kernel/core/pipe.c](kernel/core/pipe.c#L207) for pipe writes
- [kernel/net/socket.c](kernel/net/socket.c#L1183) and [kernel/net/socket.c](kernel/net/socket.c#L1309) for socket send paths
- [kernel/audio/audio_core.c](kernel/audio/audio_core.c#L682) for audio stream writes
- [kernel/driver/pty.c](kernel/driver/pty.c#L626) for PTY writes
- [kernel/fs/fs.c](kernel/fs/fs.c#L780) and [kernel/fs/vfs_ext2.c](kernel/fs/vfs_ext2.c#L350) for filesystem write-back copying

Why still risky:

- materializing a missing input page as zeros means the kernel will transmit or persist zeros that the caller never explicitly stored
- that may be acceptable for an explicitly documented anonymous scratch-buffer API, but it is not equivalent to current semantics

This category is the only one where opt-in behavior might be defensible, but only through a separate API or a very explicit policy bit, not through generic `copyin()`.

## Practical Conclusions

### Conclusion 1: Do Not Make Generic `copyin()` Self-Materializing

The surface includes syscall dispatch itself, string parsing, exec argv handling, signal restore, ioctl control blocks, socket addresses, fd masks, timeouts, and many other structured inputs.

That is too much semantic change for one helper.

### Conclusion 2: `copyin()` Materialization Is Only Defensible As An Opt-In Policy

If we ever decide that some kernel read paths should accept untouched explicit reservations as zero-filled input, the gating must be stronger than “VMA is zerofill.”

At minimum it would need:

- an explicit API contract
- a call-site-level opt-in helper distinct from generic `copyin()`
- tests that prove the affected syscall family wants zero-filled absent input rather than failure

### Conclusion 3: The Current Boundary Is Good

The current boundary is coherent:

- explicit reservations
- fault-time materialization
- `copyout()` support for syscall output buffers
- no generic `copyin()` materialization

That keeps the input side honest and the output side practical.

## Recommended Next Step

Do not prototype generic `copyin()` materialization.

If we want to explore further, the only sane next experiment is a new, clearly named helper used by one narrowly chosen raw-payload path, not by syscall argument parsing or general control structures.

Even then, it should be treated as a separate feature, not as “lazy heap is done.”