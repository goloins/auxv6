# Kernel COW/Boundary Safety Audit (2026-04-06)

## Scope

Static audit of kernel boundary handling focused on:
- syscall argument ingress
- driver ioctl pointer handling
- user/kernel memory transfer paths
- COW-safety requirements (no direct user pointer dereference in kernel)

Audited trees:
- `kernel/core`
- `kernel/driver`
- `kernel/audio`
- `kernel/net`
- `kernel/fs`

## Prime Objective

Enforce a single hard rule for user memory: all user-memory reads/writes in kernel context must go through approved copy interfaces (`copyin`, `copyout`, and string-copy wrappers built on `copyin`).

This objective is stricter than legacy xv6-style pointer validation and is intended to prevent COW boundary faults, panic-on-bad-pointer behavior, and accidental raw dereference regressions.

## Required Boundary Rule

For strict user/kernel isolation and COW safety, boundary crossings must use explicit copy helpers only:
- read from user: `copyin(...)`
- write to user: `copyout(...)`
- string ingress: bounded bytewise copy via `copyin(...)` loop (or equivalent helper)

Kernel code must not directly dereference user pointers (including pointers embedded inside ioctl payload structs).

Clarification:
- This rule applies specifically to user-memory access.
- Kernel-internal memory access remains direct.
- MMIO/device register access remains via driver/device accessors (not `copyin/copyout`).

## Findings

### Critical: syscall string path directly dereferences user memory

`fetchstr` returns a raw user pointer and scans it with direct dereference (`*s`) instead of `copyin`.
This violates strict user/kernel isolation and is not COW-safe by policy.

Primary unsafe implementation:
- `kernel/core/syscall.c:35`
- `kernel/core/syscall.c:42`
- `kernel/core/syscall.c:45`
- `kernel/core/syscall.c:80`
- `kernel/core/syscall.c:85`

Every syscall site below inherits this unsafe behavior through `argstr(...)`:
- `kernel/core/sysfile.c:1065`
- `kernel/core/sysfile.c:1102`
- `kernel/core/sysfile.c:1179`
- `kernel/core/sysfile.c:1354`
- `kernel/core/sysfile.c:1364`
- `kernel/core/sysfile.c:1473`
- `kernel/core/sysfile.c:1617`
- `kernel/core/sysfile.c:1635`
- `kernel/core/sysfile.c:1657`
- `kernel/core/sysfile.c:1713`
- `kernel/core/sysfile.c:1752`
- `kernel/core/sysfile.c:2114`
- `kernel/core/sysfile.c:2536`
- `kernel/core/sysfile.c:2836`
- `kernel/core/sysfile.c:2879`
- `kernel/core/sysfile.c:2950`
- `kernel/core/sysfile.c:2991`

Additional direct user-pointer use in exec argument expansion:
- `kernel/core/sysfile.c:2127` (`fetchstr(uarg, &argv[i])`)

Extra direct dereferences of user-derived argv pointers in debug block:
- `kernel/core/sysfile.c:2133`
- `kernel/core/sysfile.c:2135`
- `kernel/core/sysfile.c:2136`
- `kernel/core/sysfile.c:2145`
- `kernel/core/sysfile.c:2146`

Impact:
- kernel touches user memory outside approved interfaces
- weakens fault containment at boundary
- violates one-way syscall data flow policy

Additional note:
- `fetchstr(...)` also does not guard `myproc()/pgdir` before dereference setup, unlike `fetchint(...)`.

---

### Critical: audio ioctl contains nested user pointer dereference

`sys_ioctl` stages the top-level ioctl struct in kernel memory, but `AUDIO_IOC_ENUM_DEVICES` then treats a user-supplied pointer field as directly writable kernel pointer.

Unsafe instance:
- `kernel/audio/audio_core.c:950`
- `kernel/audio/audio_core.c:952`
- `kernel/audio/audio_core.c:954`
- `kernel/audio/audio_core.c:955`
- `kernel/audio/audio_core.c:956`
- `kernel/audio/audio_core.c:957`
- `kernel/audio/audio_core.c:958`
- `kernel/audio/audio_core.c:959`
- `kernel/audio/audio_core.c:960`
- `kernel/audio/audio_core.c:961`

Pattern:
- `entries_ptr` comes from user
- kernel casts it to `struct audio_device_info *`
- kernel writes through it directly instead of `copyout`

Impact:
- direct write into user virtual address space from kernel context
- bypasses approved boundary interface
- vulnerable to boundary faults/panics under malformed pointers

---

### High: tty ioctl handlers dereference raw `arg` pointers directly

The tty ioctl backend API uses `uint arg` and then performs typed dereference in drivers.
Current `sys_ioctl` generally stages pointer payloads for many request codes, but the backend contract itself is not boundary-safe and is not self-enforcing.

Console handler:
- `kernel/driver/console.c:3433`
- `kernel/driver/console.c:3451`
- `kernel/driver/console.c:3500`
- `kernel/driver/console.c:3506`
- `kernel/driver/console.c:3513`
- `kernel/driver/console.c:3526`

Serial handler:
- `kernel/driver/serial.c:653`
- `kernel/driver/serial.c:676`
- `kernel/driver/serial.c:685`
- `kernel/driver/serial.c:694`
- `kernel/driver/serial.c:704`
- `kernel/driver/serial.c:715`
- `kernel/driver/serial.c:721`
- `kernel/driver/serial.c:730`
- `kernel/driver/serial.c:765`
- `kernel/driver/serial.c:785`
- `kernel/driver/serial.c:815`
- `kernel/driver/serial.c:824`

PTY handler:
- `kernel/driver/pty.c:203`
- `kernel/driver/pty.c:219`
- `kernel/driver/pty.c:225`
- `kernel/driver/pty.c:232`
- `kernel/driver/pty.c:238`
- `kernel/driver/pty.c:247`
- `kernel/driver/pty.c:253`
- `kernel/driver/pty.c:260`
- `kernel/driver/pty.c:266`
- `kernel/driver/pty.c:275`
- `kernel/driver/pty.c:277`
- `kernel/driver/pty.c:283`
- `kernel/driver/pty.c:290`
- `kernel/driver/pty.c:292`
- `kernel/driver/pty.c:299`
- `kernel/driver/pty.c:306`
- `kernel/driver/pty.c:308`

Impact:
- backend accepts raw pointer-shaped argument and dereferences it directly
- safety depends on caller discipline, not interface contract
- violates strict “single correct way” boundary model

---

### Medium: legacy raw-pointer helper still present (`argptr`)

`argptr(...)` is still implemented as a raw user pointer validator/return helper:
- `kernel/core/syscall.c:62`

Current pass found no active call sites, but the API remains available and encodes a non-copy-based boundary model.

Impact:
- dead but dangerous interface remains in tree
- invites regressions where new syscall code reintroduces direct user-pointer dereference

## Fine-Toothed Comb (Second Pass)

Second pass covered:
- all `sys_*` entrypoints in `kernel/core`, `kernel/net`, `kernel/fs`
- all `argstr/fetchstr/fetchint/argptr` uses
- ioctl dispatch path and backend handlers for console/pty/serial/audio/tuntap
- nested pointer-field dereference patterns inside staged ioctl payloads
- suspicious integer-to-pointer casts in drivers

Confirmed still-unsafe classes:
- `fetchstr/argstr` direct string dereference model
- `sys_exec` debug block direct dereference of user-derived `argv[1]`
- audio enum ioctl nested pointer write via `entries_ptr`
- tty backend ioctl contract using raw pointer-shaped `arg`

Confirmed non-findings (reviewed and intentionally excluded):
- driver IRQ callback `void *arg` casts in files like `e1000`, `firewire`, `virtio_gpu` are internal registration context, not syscall/user boundary.
- most syscall payload paths in `sysfile`, `sysproc`, and `socket` now stage through `copyin/copyout`.
- signal-control and tty termios wrappers (`proc_sigaction`, `proc_sigprocmask`, `proc_tc*`) are copyin/copyout mediated.

## Additional Notes

- `kernel/core/sysfile.c` already contains a safe string ingress helper (`copyinstr_user`) that uses bytewise `copyin`.
- `copyinstr_user` is currently used in mount paths (`sys_mount`, `sys_umount`) but not used to replace `argstr/fetchstr` generally.
- Most non-string syscall data paths reviewed (read/write/stat/socket/ioctl staging) already use `copyin/copyout` correctly at top-level boundaries.

## Remediation Checklist (Policy-Oriented)

1. Replace `fetchstr/argstr` user-pointer dereference model with copy-based string ingress everywhere.
2. Remove direct dereference from tty ioctl backends; pass typed kernel structs or enforce copy wrappers at backend boundary.
3. Fix `AUDIO_IOC_ENUM_DEVICES` to stage output in kernel buffer and `copyout` to user `entries_ptr` entries.
4. Ban raw user pointer dereference by policy and add CI grep checks for patterns such as `*(type*)arg` in syscall/ioctl boundary code.
5. Keep scalar-only ioctl requests scalar; pointer requests must use explicit staged kernel buffers plus copy helpers.
6. Remove or hard-disable `argptr(...)` to prevent future reintroduction of raw user pointers in syscall handlers.

## Audit Result

Unsafe boundary behavior is present and documented above. The kernel does not currently enforce a single boundary-safe mechanism across all syscall and driver ioctl paths.
