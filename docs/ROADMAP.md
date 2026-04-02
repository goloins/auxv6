# auxv6 Modernization Roadmap

## Project Overview

auxv6 is an xv6-derived Unix-like operating system with significant enhancements including:
- Multi-backend VFS layer (xv6fs, ext2, msdosfs, procfs)
- BSD-style networking with TCP/IP stack
- Signal handling infrastructure
- Job control with process groups, sessions, terminal control
- Multi-filesystem support with mount table

**Architecture:** x86 32-bit, single address space per process  
**Current State:** Educational OS with working core components, ext2-root boot as the default path, and a growing POSIX-style userland, but still missing several modern drivers and full POSIX compliance

---

## Recent Progress (2026-03-30 to 2026-04-02)

- **libc ABI cleanup phase 5 bridge removal landed 2026-04-02:** the temporary native-header bridge is gone. `include/auxv6/user.h` now declares plain `exit(int)` and `dprintf(...)` with no `exit(...)` variadic shim or `printf` macro remap, and `user/stdlib.c` now exports the public `exit` symbol directly. The in-tree source sweep is complete for that bridge surface, with no remaining `printf -> dprintf`, variadic `exit(...)`, or `__auxv6_libc_exit` references in tracked source. Validation passed with direct compile probes against the cleaned header surface, `make aux.kern`, and `sudo make -f ports/dash-0.5.12/Makefile.auxv6 clean all`; the dash rebuild still links successfully and only emits pre-existing warning noise from the compatibility headers and `signames.c`.
- **libc ABI cleanup phase 5 smoke follow-up landed 2026-04-02:** a user `nslookup` compile smoke caught one remaining header-level fd-style `printf` dependency in `user/netcommon.h` that the earlier source-only sweep had missed. That helper header now uses explicit `dprintf(...)`, the fd-style `printf` audit is clean across `user/*.h`, `user/*.c`, and `user/*.S`, and a host-side rebuild of `_nslookup`, `_ifconfig`, `_netinfo`, `_route`, `_arp`, and `_ip` passed afterward.

- **libc ABI cleanup phase 5 core landed 2026-04-01:** user binaries now link at `_start` via new `user/crt0.S` instead of entering directly at `main`; the raw user syscall wrappers for `exit` and `getcwd` were renamed to private `__auxv6_sys_*` entry points; the kernel exit path now carries an explicit status from `sys_exit()` into wait status; `user/stdlib.c` now owns the real libc `exit`/`_Exit` path; `user/posix_fs.c` now owns standard `getcwd`; and `user/printf.c` plus `user/stdio.c` split the old fd-based `printf` surface into native `dprintf`/`vdprintf` and real stdio `printf`/`vprintf`. Validation passed via direct mixed-header probes, a normal `make aux.kern`, representative object compilation including `sh`, `init`, `termcheck`, and `usertests`, and representative links for `pwd`, `echo`, `grep`, `ls`, `file`, `lsof`, `forktest`, `sh`, and `init` under the new `_start` entry.
- **libc cleanup phase 4 landed 2026-04-02:** `include/auxv6/user.h` was harmonized against canonical `string.h`, `stdlib.h`, and `unistd.h` where the existing ABI already matched, shared prototypes now use canonical POSIX/C type spellings, native declarations that still intentionally differ are guarded to avoid mixed-include collisions, auxv6 `exit(...)` compatibility now works in both include orders through a shared alias-and-macro path, and the temporary `user/errstr.c` forward declaration workaround was removed. Validation passed via direct mixed-header probes plus a scratch replay of representative userland compile/link rules, used because this workspace still has root-owned generated artifacts that can break post-link `*.sym` generation on ordinary staged user builds.
- **libc cleanup phase 3 landed 2026-04-01:** `user/ulib.c` was reduced to the small xv6-era core runtime while extended string helpers moved into `user/string.c` and `errno` plus string-based error reporting moved into `user/errstr.c`; `Makefile` now links those objects explicitly; and a representative sudo rebuild covering shell, admin, network, staged tools, and `aux.kern` completed successfully after resolving one local header-surface conflict in `errstr.c`.
- **libc cleanup phase 2 landed 2026-04-01:** the remaining userland include sweep was completed, removing legacy `../include/...`, plain `user.h`, and `posix/...` paths from `user/*.c` and `user/*.S`; `user/env.c`, `user/stdlib.c`, and `user/posix_fs.c` were split out of `user/ulib.c` and `user/posix.c`; `openpty()` moved into `user/tty.c`; and both a widened sudo userland build and `sudo make aux.kern` completed successfully afterward.
- **libc cleanup phase 1 started 2026-04-01:** canonical top-level `stdarg.h`, `ctype.h`, `dirent.h`, and `sys/ioctl.h` headers were added; the old `include/posix/*` copies for those paths were reduced to compatibility wrappers; `include/auxv6/user.h` now carries the native auxv6 user ABI while legacy `include/user.h` remains as a shim; the first wave of libc and tty-facing sources now includes canonical header paths; `stpcpy()` moved out of `user/posix.c` into `user/ulib.c`; and the first real breakup landed with formatting, dirent, tty, and inet helpers split into `user/fmt.c`, `user/dirent.c`, `user/tty.c`, and `user/inet.c`.
- **libc compatibility tranche landed 2026-04-01:** userspace regex support (`regcomp/regexec/regerror/regfree`) and a baseline `FILE *` stdio layer (`fopen/fclose/ferror/fflush`, `fdopen`, `fmemopen`, `getline/getdelim`, `fprintf/vfprintf`, `puts/fputs/fgetc/fputc`) are now available for third-party ports. A dedicated aux build path for upstream `sbase` grep (`ports/sbase/Makefile.auxv6`) now builds a minimally patched port and stages it as `sgrep` (`/bin/sgrep`) while keeping upstream source changes minimal.
- **Userland diagnostics tranche landed 2026-04-01:** new `which`, `lsof`, and baseline `file` utilities are now part of the system image, with manpages staged under `/usr/share/man` for all three commands.
- **procfs gained open-file visibility for userspace tooling 2026-04-01:** `/proc/lsof` now exposes per-process open fd snapshots (pid, fd, type, rw, dev, ino, off, name), backed by a new kernel `proc_fd_snapshot()` path used by `lsof`.
- **Loop device hardening tranche landed 2026-04-01:** `loop_setup()` now validates offset alignment, EOF bounds, nblocks range, and backing inode type. `loop_teardown()` is guarded by a new `vfs_dev_is_mounted()` helper that blocks detach while a filesystem is mounted on the device. `loopstatus()` API extended to return `offset` and a `flags` word with `LOOP_STATUS_MOUNTED`. `losetup` list output updated to show offset and mounted columns. Dedicated regression suite landed as `user/looptest.c` with three test groups: setup validation, status metadata, and busy-teardown guard. Test ISO image (`targetfs/tmp/test.iso`, containing README, HELLO, and DATA files) added to the rootfs staging path and used by the busy-teardown group. Rootfs image expanded to 256 MB for headroom.
- **`cprintf` format support expanded 2026-04-01:** Kernel `cprintf` now supports field width, precision (both `%.N` and `%.*`), left-align (`-`), zero-pad (`0`) flags, `%c`, `%o`, `%i`, and length modifier (`l`). The old minimal dispatcher was replaced with a `cprintint_w`-based loop matching the capability of the userspace `printf`. This removes the need for workarounds when writing diagnostic code that uses standard printf format strings.
- **isofs diagnostic prints gated 2026-04-01:** All mount-time diagnostic `cprintf` calls in `vfs_isofs.c` are now wrapped in `MOUNTDBG(...)`, silenced unless `DBG_MOUNT`/`AUXV6_DEBUG` is enabled. Only error paths remain unconditional. `docs/DEBUG-FLAGS.md` updated to note isofs under `DBG_MOUNT`.
- Signal delivery, `alarm()`, `SIGPIPE`, `lseek`, `dup2`, and baseline `fcntl()` support landed and are now integrated into the main syscall path.
- PCI enumeration, IRQ registration, DMA allocation helpers, and `lspci` landed as the Tier 2 device foundation.
- ext2 is now the default root filesystem build target, staged images are created with correct `root:root` ownership, and init is executed from the mounted root filesystem after VFS initialization.
- Virtio infrastructure moved from scaffolding to working code: `virtio-blk` now probes, negotiates features, performs block I/O, registers with the block layer, and shows up through `lsblk` and `/dev/vd*` nodes.
- The network stack moved beyond loopback-only behavior: Ethernet framing, ARP cache/request/reply, routing controls, virtio-net RX/TX, DHCP tooling, resolver/`nslookup`, and outbound internet ping all landed.
- TCP now exchanges real packets with a basic three-way handshake and ACKed payload delivery; `telnet` and `netcat` were added as rough but functional userland validation tools.
- Symbolic link support moved from plumbing to working behavior: VFS-level symlink follow/no-follow resolution is in place, ext2 supports fast symlink create/read, loop-depth limits are enforced, and symlink regression tests now cover basic, chained, intermediate-path, loop, and relative-target cases.
- Loop devices landed as a new storage/mounting bridge: 8 loop block devices can now be backed by regular files through new `loopsetup`, `loopteardown`, and `loopstatus` syscalls, with `losetup` userspace support.
- Baseline I/O multiplexing landed with `select()` and `poll()` syscalls, including fd readiness across inode files, pipes, and sockets plus timeout handling.
- ISO 9660 moved from a broken stub to a working read-only filesystem with real VFS integration, primary volume descriptor parsing, directory traversal, case-insensitive lookup, and file reads via loop-mounted images.
- POSIX porting work expanded substantially: new `include/posix/*` headers, broader libc-style helpers in `user/ulib.c`, formatting/dirent wrappers in `user/posix.c`, `setjmp`, and enough compatibility to experiment with a `dash` port.
- Userland bootstrap is now more Unix-like: `init` runs `dash /etc/rc.d/rc.S`, tracks runlevels, handles `telinit` requests via `SIGHUP`, and `exec` supports `#!` interpreter scripts.
- Terminal compatibility moved forward: Linux-compatible tty ioctl numbers now include `TCGETS/TCSETS*`, `FIONREAD/TIOCINQ`, and `TIOCOUTQ`, with auxv6-specific `TIOCISATTY` moved to `0x54A3` to avoid Linux collision.
- PTY support moved from baseline to dynamic behavior: kernel major 3 PTY backend now allocates multiple PTY pairs via `/dev/ptmx`, tracks per-file endpoint identity, exposes `TIOCGPTN`, and provides queueing plus winsize/termios ioctls and `SIGWINCH` signaling.
- Init/userland terminal setup improved further: `init` now pre-creates `/dev/pts/0..15`, `openpty()`/`ptsname_r()` use dynamic `/dev/pts/N` resolution, and `termcheck` now includes multi-PTY isolation/lifecycle plus max create/terminate stress coverage.
- Terminal parser compatibility tranche landed 2026-04-01: console ANSI handling now applies multi-parameter DECSET/DECRST mode lists (`CSI ? ... h/l`), supports autowrap enable/disable (`?7`), and adds insert-mode toggling (`CSI 4 h/l`) in the output path. `termcheck` now validates `isatty`/`ttyname`/`ttyname_r` identity behavior across console, PTY master (`/dev/ptmx`), and PTY slave (`/dev/pts/N`) fds.
- PTY control semantics tranche landed 2026-04-01: PTY `TIOCSCTTY` is no longer a no-op and now initializes PTY foreground process group to the caller `pgid`; `termcheck` gained `TIOCSCTTY` + `TIOCSPGRP/TIOCGPGRP` PTY ioctl roundtrip coverage.
- Terminal automation tranche landed 2026-04-01: guest harness coverage now includes `tools/tests/termcheck-smoke.cmds` and `make test-termcheck-smoke` for scripted tty/pty/termios regression smoke runs.
- Terminal parser extension tranche landed 2026-04-01: console now supports additional CSI operations used by full-screen apps (`CSI X`, `CSI b`, `CSI d/e/a`, `CSI ! p` soft reset), `termdemo` exercises these paths, and guest automation now includes `tools/tests/termdemo-smoke.cmds` via `make test-termdemo-smoke`.
- Terminal job-control tranche landed 2026-04-01: PTY slave read/write paths now enforce background semantics (`SIGTTIN` on background read, `SIGTTOU` with `TOSTOP` on background write), and `termcheck` gained explicit per-PTY background signal isolation checks.
- PTY job-control follow-up 2026-04-01: background signal enforcement is now gated to sessions that explicitly established controlling PTY state via `TIOCSCTTY`, avoiding false positives for simple non-controlling PTY roundtrip paths.
- Guest automation on macOS now defaults to sudo-aware launch flow for QEMU targets (`sudo -n make` via `AUXV6_MAKE_CMD`), with fast-fail guidance to run `sudo -v` before test runs.
- Guest automation harness follow-up 2026-04-01: command completion no longer depends solely on prompt matching; `tools/qemu-guest-test.exp` now uses explicit command-done markers to avoid false boundaries in ANSI/TUI-heavy output (for example `termdemo`).
- Guest automation cleanup follow-up 2026-04-01: failed `qemu-guesttest-template` runs now force-kill stale `qemu-system-i386` host processes so subsequent test runs do not inherit leaked background QEMU instances.
- `termdemo` guest smoke follow-up 2026-04-01: validation mode switched to completion-based execution (hang/crash detection) instead of brittle fixed-string output assertions to align with minimal-shell/TUI output variability.
- Terminal query-response tranche landed 2026-04-01: console now replies to terminal status/device queries (`CSI 5n`, `CSI 6n`, `ESC Z`, `CSI c`) and `termcheck` gained explicit DSR/DA verification coverage.
- Terminal query/parser follow-up landed 2026-04-01: console now answers secondary DA (`CSI > c` -> `ESC[>0;0;0c`), and `termcheck` now validates DEC-private CPR (`CSI ?6n`) plus stricter cursor parser probes for wrap toggle (`CSI ?7 h/l`), erase-char (`CSI X`), repeat (`CSI b`), and insert-mode toggle (`CSI 4 h/l`).
- Terminal harness/parser stabilization follow-up 2026-04-01: console cursor writes are now clamped to screen bounds before CPR reporting, `termcheck` CPR parsing now prefers the latest valid reply in mixed-response buffers, and `tools/qemu-guest-test.exp` now falls back to `Ctrl-A x` if guest `halt` does not terminate QEMU promptly.
- Terminal smoke runtime follow-up 2026-04-01: `termcheck` now supports a fast `--smoke` mode, and `tools/tests/termcheck-smoke.cmds` uses it to avoid long-run/interactive-sensitive checks in harness runs while preserving full coverage in normal `termcheck` execution. Smoke mode now also skips ANSI query/parser probes (`DSR/DA`, cursor-report sequences) to avoid cursor-motion output corruption and host-terminal reply pollution on `qemu-nox` runs.
- Terminal full-regression automation follow-up 2026-04-01: `tools/tests/termcheck-full.cmds` and `make test-termcheck-full` now provide a dedicated full (non-smoke) terminal/PTY regression path through the shared guest harness.
- Terminal full-regression validation follow-up 2026-04-01: `make test-termcheck-full` now completes with `termcheck: all checks passed` in the guest automation flow, and an aggregate `make test-terminal-regression` target now runs full termcheck plus termdemo smoke in sequence.
- Terminal mode-query tranche landed 2026-04-01: console now supports RMQ/DECRQM-style mode reports (`CSI Ps $ p`, `CSI ? Ps $ p` -> `... $ y` replies), and `termcheck` full mode now verifies insert-mode and cursor-visibility query transitions.
- Terminal DEC/private matrix follow-up landed 2026-04-01: `termcheck` full mode now covers additional private-mode query transitions (`?7`, `?6`), unknown-mode query fallback responses (`Pm=0`), and origin/scroll-region cursor invariants under `DECSTBM` + DECOM.
- Terminal RMQ correctness follow-up 2026-04-01: console numeric query reply serialization now preserves zero values (`Pm=0`) so unknown-mode RMQ/DECRQM responses are reported correctly and no longer mis-encoded as `Pm=1`.
- Terminal mode-matrix expansion follow-up 2026-04-01: full `termcheck` RMQ/DECRQM coverage now includes private-mode transitions for reverse video (`?5`) and cursor blink (`?12`) plus standard newline mode (`20`) in addition to existing `?6/?7/?25` and insert-mode checks.
- Terminal curses-facing follow-up 2026-04-01: full `termcheck` now also validates cursor-keys app mode (`?1`) and alt-screen mode (`?1049`) query transitions, plus alt-screen cursor save/restore invariants (enter at home, exit restores prior cursor position).
- Terminal capability follow-up 2026-04-01: staged `/etc/termcap` now includes additional fullscreen/curses-facing capabilities (`ti/te`, `ks/ke`, `vi/ve/vs`, `im/ei`, `al/dl/dc/ic`, movement aliases), and guest automation adds `make test-termcap-smoke` to verify those entries remain present.
- Terminal capability smoke follow-up 2026-04-01: `tools/tests/termcap-smoke.cmds` now uses shell-agnostic `RAW cat /etc/termcap` + harness-side `EXPECT` assertions instead of guest `grep` options, avoiding xv6-style grep flag limitations.
- Terminal full-check stability follow-up 2026-04-01: full `termcheck` now isolates the noncanonical queue-state probe onto a PTY path when available and drains/flushed tty query-reply bytes between query-heavy checks to avoid cross-check interference and host-prompt input pollution.
- Terminal observability follow-up 2026-04-01: `termcheck` now writes PASS/FAIL/SKIP summary lines to a guest log file (`/tmp/termcheck.log` by default; configurable via `--log`) so full-run diagnostics remain readable when terminal ANSI output interleaves on console.
- Guest harness host-terminal safety follow-up 2026-04-01: raw guest output passthrough is now disabled by default in `tools/qemu-guest-test.exp` (`AUXV6_LOG_USER=0`) to prevent ANSI query/reply bytes from polluting the host shell input after QEMU exits.
- Device-node lifecycle moved from static init hardcoding to a runlevel-integrated userspace manager: `devman -s` now runs in `rc.S`, scans kernel-visible devices, creates `/dev` nodes dynamically, and supports `debug=0/1` tuning via `/etc/devman.conf`.
- Userland discoverability improved: `man` support and initial manpage coverage landed, with generation workflow documented in `docs/man-pages.md` and helper tooling in `tools/gen-man-pages.sh`.
- Dash porting on Linux hosts became more robust: aux build rules now force-regenerate host tools and include `libgcc_compat.c` fallbacks for 64-bit division/mod helpers.
- procfs gained more than basic process plumbing: `/proc/uptime`, `/proc/pci`, `/proc/vblk_flush`, and `/proc/ahci_tune` now exist for observability and runtime tuning.
- Virtio-blk regression coverage now includes a dedicated in-guest `vblktest` utility plus `qemu-virtioblktest` / `qemu-nox-virtioblktest` launch targets for multi-disk validation.
- Virtio transitional PCI probe matching was corrected in virtio-blk/net/gpu init scans (`device_id - 0x0FFF`), preventing net/gpu misprobes from resetting other virtio functions during mixed-device boots.
- Reusable QEMU guest automation template landed: `tools/qemu-guest-test.exp` runs command scripts (`tools/tests/*.cmds`) after login, with `make qemu-guesttest-template` plus `make test-virtioblk-smoke` and `make test-virtioblk-negative` coverage.
- Retry-class validation coverage was added for virtio-blk via `make test-virtioblk-retry-stress`, using deterministic fault injection (`test_fail_mode` / `test_fail_count`) and assertion checks for bounded retry semantics, including per-device `last_fail_class` observability.
- Guest automation now supports shell-independent `EXPECT` assertions inside command scripts, with optional rc checking (`AUXV6_CHECK_RC=1`) for richer shells and a basic-shell-safe default path.
- Buffer-cache I/O error handling was hardened: `bread`/`bwrite` no longer panic immediately on transport failures, buffer error state is tracked (`B_ERROR`/`berror()`), and ext2/msdos/isofs/blockdev plus legacy xv6fs log/fs paths now explicitly gate on I/O error instead of silently consuming failed buffers.
- The I/O-failure check path is now centralized below VFS via shared bio helpers (`bread_ok`/`bwrite_ok`), so active filesystems use one common block-level contract instead of duplicating `bread`/`berror`/`bwrite` checks per backend.
- VFS backend lookups are now mount-aware for ext2/msdosfs/isofs instances, avoiding global active-device aliasing when resolving `/` inside non-root mounts.
- Guest shutdown now has a first-class `halt` utility backed by a kernel poweroff syscall, so QEMU exit no longer depends on host-side `killall`.
- **NVMe driver** now has I/O queue creation and synchronous READ/WRITE command support via PRP1 (single-page transfers).
- **E1000 driver** (Intel Gigabit Ethernet) now has full ifnet integration with TX/RX descriptor rings, IRQ handling, and proper network interface registration.
- **PCNET driver** (AMD PCNET-PCI II) now has full ifnet integration with TX/RX rings, initialization block, and IRQ handling.
- **RTL8111 driver** (Realtek Gigabit Ethernet) promoted from probe stub to full implementation with descriptor-based TX/RX, IRQ handling, and ifnet integration.
- **VMXnet3 driver** stub added for VMware paravirtualized NIC support (PCI detection, BAR mapping, MAC address reading).
- **Hyper-V NetVSC driver** stub added for Microsoft Hyper-V synthetic network adapter (requires VMBus infrastructure for full implementation).
- **Intel I219-V driver** stub added for PCH-integrated Intel Ethernet (BSD-style attach skeleton with PCI probe, BAR mapping, MAC/link readout, and ifnet registration).
- **Intel I226-V driver** stub added for Intel 2.5GbE bring-up (IGC-style PCI attach skeleton, BAR mapping, MAC/link readout, ifnet registration).
- **ASIX AX88179 PCI driver** stub added as a PCI-only scaffold (explicitly avoiding xHCI/USB for now).
- Virtio-blk mount/persistence stress pass landed: `make test-virtioblk-mount-stress` mounts a host-pre-formatted ext2 volume via virtio-blk, writes data across 4 mount/umount cycles, and asserts persistence on every remount — completing the virtio-blk definition-of-done checklist.

---

## Current Subsystem Status

### ✅ Mature Subsystems (75-95% complete)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| VFS Layer | 90% | Multi-backend, mount table, longest-prefix matching |
| ext2 filesystem | 85% | Read/write, directories, inode management, default rootfs build target |
| FAT/msdosfs | 80% | ~1650 LOC, FAT12/16/32, short/long filenames |
| TCP/IP stack | 80% | UDP works, DNS/resolver works, TCP handshake + data + retransmission + teardown work; flow control still basic |
| Process model | 85% | fork/exec/wait, process groups, sessions |
| Job control | 80% | setpgid, setsid, tcsetpgrp, terminal control |
| Signal handling | 95% | Full userspace delivery, alarm(), SIGPIPE, hardware fault mapping |
| Bootstrapping / init | 80% | VFS-launched init, rc scripts, runlevels, telinit, shebang exec, early-runlevel `devman` device-node bootstrap |
| Memory management | 80% | Virtual memory, page tables, kalloc/kfree |
| PCI subsystem | 80% | Bus 0 enumeration, BAR decode/mapping, helper APIs, `lspci`; MSI/MSI-X still missing |
| DMA support | 75% | Page-based DMA allocation with physical address tracking and alignment |
| Loop devices | 85% | 8 block devices; setup validation hardened, busy-teardown guard, extended status (offset + mounted flag), `looptest` regression suite, 256 MB rootfs |
| ISO 9660 | 85% | Working read-only implementation with VFS integration and loop-mount testing |
| Symlinks | 90% | `symlink/readlink/lstat` wired, VFS follow/no-follow behavior landed, ext2 path traversal follows intermediate links, loop-depth limits and regression tests added |
| Terminal/PTY stack | 85% | Console + dynamic PTY allocation (`/dev/ptmx` -> `/dev/pts/N`), per-endpoint queue/termios/winsize/ioctl routing, stress-tested create/terminate lifecycle, and dynamic node creation via `devman` |

### ⚠️ Partially Implemented (50-74%)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| Networking interfaces | 60% | BSD ifnet abstraction, loopback, virtio-net, routing, DHCP tooling, outbound packet path |
| POSIX compatibility layer | 70% | Broader tty/ioctl compatibility, dynamic `openpty`/`ptsname_r` path, dash portability fixes; many APIs still stubbed or partial |
| Userland docs/manpages | 72% | `man` utility plus baseline pages are available, including new `which`/`lsof`/`file` coverage; command coverage and completeness are still growing |
| procfs | 75% | `/proc/uptime`, `/proc/version`, `/proc/pci`, `/proc/vblk_flush`, `/proc/ahci_tune`, `/proc/meminfo`, `/proc/ps`, `/proc/mountstats`, `/proc/gfxstats`, `/proc/lsof`; breadth improved but still sparse overall |
| Virtio storage | 95% | Working virtio core + virtio-blk, shared IRQ-safe under QEMU multi-device setups, with dedicated `vblktest` regression coverage, retry telemetry, per-device failure class observability, fault injection, mount/persist stress harness, and queue-depth knob infrastructure — full DoD checklist complete |
| Real NICs | 60% | E1000, PCNET, RTL8111 have full ifnet integration; VMXnet3, Hyper-V netvsc, Intel I219-V, Intel I226-V, and ASIX AX88179 PCI are stubs |
| Device node management | 70% | `devman -s` creates `/dev` nodes at early runlevel from kernel-visible inventory; hotplug/event mode and richer policy rules still pending |

### 🚧 Early Or Stubbed (0-49%)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| Modern storage | 40% | AHCI has polling DMA read/write; NVMe has I/O queue and basic RW path |
| Btrfs | None | Planned read-only support |
| NFS | None | Planned; requires XDR/RPC infrastructure |
| Device hotplug/eventing | None | Planned kernel event path for live node add/remove beyond boot-time `devman -s` |

---

## Priority Tier 1: Foundation (Weeks 1-4)

These items are blocking everything else and must be done first.

### 1.1 Signal Delivery to Userspace [COMPLETE]
**Status:** Implemented 2026-03-30  
**Files:** `proc.c:proc_deliver_signal()`, `sysproc.c:sys_sigreturn()`, `trap.c`, `signal.h`, `vm.c:copyin()`  
**Implementation:**
1. Signal frame (`struct sigframe`) pushed onto user stack with saved registers
2. Trampoline code embedded in frame calls `sigreturn` syscall
3. `proc_deliver_signal()` called from `trap.c` before returning to userspace
4. `sigreturn` restores original context via `copyin()` from user stack
5. Signal mask saved/restored properly

**Signals Implemented (POSIX-compatible numbering):**
| Signal | # | Default | Notes |
|--------|---|---------|-------|
| SIGHUP | 1 | term | Terminal hangup |
| SIGINT | 2 | term | Interrupt (Ctrl+C) |
| SIGQUIT | 3 | term | Quit (Ctrl+\\) |
| SIGILL | 4 | term | Illegal instruction (from trap.c) |
| SIGTRAP | 5 | term | Breakpoint/debug (from trap.c) |
| SIGABRT | 6 | term | Abort |
| SIGBUS | 7 | term | Alignment fault (from trap.c) |
| SIGFPE | 8 | term | FPU/divide error (from trap.c) |
| SIGKILL | 9 | term | Kill (uncatchable) |
| SIGUSR1 | 10 | term | User-defined 1 |
| SIGSEGV | 11 | term | Segfault/GPF (from trap.c) |
| SIGUSR2 | 12 | term | User-defined 2 |
| SIGPIPE | 13 | term | Broken pipe (implemented in pipewrite) |
| SIGALRM | 14 | term | Alarm (alarm() syscall implemented) |
| SIGTERM | 15 | term | Termination |
| SIGCHLD | 17 | ignore | Child status change |
| SIGCONT | 18 | cont | Continue |
| SIGSTOP | 19 | stop | Stop (uncatchable) |
| SIGTSTP | 20 | stop | Terminal stop (Ctrl+Z) |
| SIGTTIN | 21 | stop | Background tty read |
| SIGTTOU | 22 | stop | Background tty write |
| SIGWINCH | 28 | ignore | Window resize |

**Estimate:** 3-4 days

### 1.2 lseek Syscall [COMPLETE]
**Status:** Implemented 2026-03-30  
**Files:** `kernel/core/sysfile.c`, `include/syscall.h`, `include/fcntl.h`, `user/usys.S`  
**Implementation:**
- Added `SYS_lseek` (syscall 66) supporting SEEK_SET, SEEK_CUR, SEEK_END
- Returns new offset on success, -1 on failure
- Cannot seek on pipes or sockets (returns -1)
- Validates for negative resulting offsets

### 1.3 dup2 Syscall [COMPLETE]
**Status:** Implemented 2026-03-30  
**Files:** `kernel/core/sysfile.c`, `include/syscall.h`, `user/usys.S`  
**Implementation:**
- Added `SYS_dup2` (syscall 67) to duplicate fd to specific number
- Closes newfd if already open (POSIX behavior)
- Returns newfd on success, handles oldfd==newfd case correctly

### 1.4 fcntl Syscall [COMPLETE]
**Status:** Implemented 2026-03-30  
**Files:** `kernel/core/sysfile.c`, `include/syscall.h`, `include/fcntl.h`, `user/usys.S`  
**Implementation:**
- Added `SYS_fcntl` (syscall 68)
- F_DUPFD: duplicate to lowest fd >= arg
- F_GETFD/F_SETFD: get/set fd flags (FD_CLOEXEC stub)
- F_GETFL/F_SETFL: get/set file status flags (O_RDONLY/O_WRONLY/O_RDWR)
- F_DUPFD_CLOEXEC: duplicate with close-on-exec (stub)
- Note: FD_CLOEXEC and O_APPEND not fully tracked yet

**fcntl.h enhanced with:**
- O_CREAT, O_EXCL, O_NONBLOCK, O_NOCTTY, O_CLOEXEC flags
- F_DUPFD, F_GETFD, F_SETFD, F_GETFL, F_SETFL, F_DUPFD_CLOEXEC commands
- FD_CLOEXEC flag
- SEEK_SET, SEEK_CUR, SEEK_END whence values

---

## Priority Tier 2: Device Infrastructure (Weeks 5-8) [COMPLETE]

### 2.1 PCI Subsystem [COMPLETE]
**Status:** Implemented 2026-03-30  
**Files:** `kernel/driver/pci.c`, `include/pci.h`  
**Implementation:**
- [x] PCI bus enumeration (bus 0, all slots/functions)
- [x] Config space read/write (I/O ports 0xCF8/0xCFC)
- [x] BAR decoding and size detection
- [x] BAR mapping to virtual memory (MMIO via DEVSPACE)
- [x] Device lookup: pci_find_device(), pci_find_class()
- [x] Command register helpers: pci_set_master(), pci_enable_io/mem()
- [ ] MSI/MSI-X interrupt setup (future)
- [x] Device driver registration framework (struct pci_driver)

### 2.2 Interrupt Routing Modernization [COMPLETE]
**Status:** Implemented 2026-03-30  
**Files:** `kernel/core/trap.c`  
**Implementation:**
```c
typedef void (*irq_handler_t)(int irq, void *arg);
int irq_register(int irq, irq_handler_t handler, void *arg, const char *name);
int irq_unregister(int irq, const char *name);  // Supports shared interrupts; name identifies handler to remove
```
- Dynamic IRQ table with up to 256 handlers
- Automatic dispatch from trap() for IRQs 0-255
- Handler name tracking for debugging

### 2.3 DMA Abstraction [COMPLETE]
**Status:** Implemented 2026-03-30  
**Files:** `kernel/driver/dma.c`, `include/defs.h`  
**Implementation:**
```c
void *dma_alloc(uint size, uint *phys_addr);
void dma_free(void *vaddr, uint size);
void *dma_alloc_aligned(uint size, uint align, uint *phys_addr);
```
- Simple page-based allocation with physical address tracking
- Supports up to 64 concurrent DMA allocations
- Alignment support for device requirements

---

## Priority Tier 3: Storage Drivers (Weeks 9-12)

### 3.1 Virtio-blk Driver [ADVANCED PARTIAL]
**Status:** Multi-device cleanup + capability tracking + flush cadence tuning landed 2026-03-31  
**Files:** `kernel/driver/virtio.c`, `kernel/driver/virtio_blk.c`, `include/virtio.h`, `user/lsblk.c`, `user/mount.c`, `user/init.c`  
**Value:** QEMU testing, cloud deployment  
**Implemented:**
- [x] PCI detection (vendor=0x1AF4, device=0x1001)
- [x] Virtqueue setup
- [x] Feature negotiation
- [x] Block read/write commands
- [x] Integration with bdevsw

**Current behavior:**
- Registers virtio disks with the block layer and exposes them as `/dev/vd*`
- `lsblk` reports virtio disks and `mount` accepts `vd*` / `vd*pN` device names
- Init creates matching device nodes at boot when virtio disks are present

**Completion plan (finish from partial to production-ready baseline):**
- [x] Replace global-device shortcuts with dev->softc lookup in `rw`/`nblocks` paths
- [x] Add explicit device capability tracking (`FLUSH`, `DISCARD`, `WRITE_ZEROES`) at probe time
- [x] Implement `flush` request path and wire `fsync`-style call sites where available
- [x] Implement discard/write-zeroes request helpers behind capability checks
- [x] Add error accounting and robust retry policy for transient I/O failures
- [x] Add runtime flush cadence tuning (`/proc/vblk_flush`) for write-heavy workloads
- [x] Add optional queue-depth tuning knobs (`queue_depth` global + per-device `vq_size` observability via `/proc/vblk_flush`; single-queue serialized baseline, knob infrastructure plumbed for future multi-inflight)

**Definition of done:**
- [x] Multiple virtio disks can be attached and independently read/written/mounted
- [x] No hardcoded device-0 behavior remains in I/O and capacity paths
- [x] Flush/discard/write-zeroes are feature-gated and return deterministic errors when unsupported
- [x] Stress pass: repeated mount/write/umount/remount cycles complete without data corruption (`make test-virtioblk-mount-stress`)

**Dependencies:** PCI, Virtio core  
**Estimate:** 1-2 weeks

### 3.2 AHCI/SATA Driver [ADVANCED PARTIAL]
**Status:** SATA identify + blockdev registration + polling DMA read/write + timeout/recover diagnostics landed; interrupt and queue-depth work still pending  
**Files:** `kernel/driver/ahci.c`, `include/pci.h`, `include/blockdev.h`  
**Value:** Real hardware support  

**Basic implementation plan (minimum viable AHCI):**
- [x] Add per-port block device registration for detected SATA disks
- [x] Implement single-slot DMA read/write path (non-NCQ, polling first)
- [x] Build/submit Register H2D FIS for `READ_DMA_EXT` / `WRITE_DMA_EXT`
- [x] Implement timeout + error reset flow (`PxTFD`, `PxSERR`, `PxIS`) for failed commands
- [x] Read `IDENTIFY DEVICE` to populate capacity and sector size for `bdev_set_nblocks`

**Follow-up hardening (after basic works):**
- [ ] Move from polling to interrupt-assisted completion
- [ ] Support additional command slots and batched I/O
- [ ] Add ATAPI path split (kept out of MVP)
- [x] Add runtime AHCI timeout/counter tuning and observability (`/proc/ahci_tune`)

**Definition of done (basic):**
- [x] At least one SATA disk appears in `lsblk` as a blockdev
- [x] Read/write of filesystem blocks succeeds on QEMU AHCI controller
- [ ] Mount/unmount cycle succeeds repeatedly without controller lockup

**Dependencies:** PCI, DMA  
**Estimate:** 2-3 weeks

### 3.3 NVMe Driver [PARTIAL]
**Status:** Controller reset + admin queue + identify + I/O queue + basic RW path implemented  
**Files:** `kernel/driver/nvme.c`, `include/pci.h`, `include/blockdev.h`  
**Value:** Modern SSD support  

**Basic implementation plan (minimum viable NVMe):**
- [x] Finish namespace discovery (`IDENTIFY NS`) and choose active namespace policy (nsid 1 first)
- [x] Create one I/O queue pair and wire queue doorbells correctly for data commands
- [x] Implement synchronous `READ`/`WRITE` command path using PRP1 (single-page transfers)
- [x] Register namespace as block device and report capacity from namespace metadata
- [ ] Add queue timeout/completion error handling and controller reset-on-fatal fallback

**Follow-up hardening (after basic works):**
- [ ] Multi-queue per-CPU scaling
- [ ] Flush/write-zeroes/dataset-management support
- [ ] Interrupt-driven completions and MSI-X when available

**Definition of done (basic):**
- [x] NVMe namespace appears in `lsblk` and can be mounted
- [x] Buffered block read/write path passes filesystem smoke tests
- [ ] Controller recovers from command timeout without requiring full reboot

**Dependencies:** PCI, DMA  
**Estimate:** 2-3 weeks

### 3.4 Storage Bring-up Order (Recommended)
1. Virtio-blk cleanup and feature-complete baseline (fastest path to stable storage tests)
2. AHCI minimum viable read/write path (real hardware compatibility)
3. NVMe minimum viable namespace I/O (modern hardware path)
4. Shared reliability pass: timeout policy, error telemetry, and stress testing across all three drivers

### 3.5 Storage Validation Matrix
- [ ] Single-disk boot and root mount on each backend (virtio-blk, AHCI, NVMe)
- [x] Multi-disk virtio-blk attach plus in-guest enumeration/mount/I/O regression path via `vblktest`
- [ ] Multi-disk enumerate/mount behavior with mixed backends
- [ ] Large sequential read/write soak (no panic, no leaked DMA buffers)
- [ ] Power-cycle/reboot persistence check for written data
- [ ] Negative tests: missing device, command timeout, and media error behavior

---

## Priority Tier 4: Network Stack (Weeks 13-18)

### 4.1 Ethernet Layer [COMPLETE]
**Status:** Implemented 2026-03-31  
**Files:** `kernel/net/ethernet.c`, `kernel/net/device.c`, `include/net.h`  
**Tasks:**
- [x] Frame encapsulation/decapsulation
- [x] MTU handling
- [x] Protocol demux (ETHERTYPE_IP, ETHERTYPE_ARP)

**Implementation notes:**
- Pads short frames, handles broadcast/directed traffic, and demultiplexes incoming frames to IP or ARP
- Integrates with the `ifnet` output/input path instead of a loopback-only shortcut

**Estimate:** 3-4 days

### 4.2 ARP Implementation [COMPLETE]
**Status:** Implemented 2026-03-31  
**Files:** `kernel/net/arp.c`, `user/arp.c`  
**Tasks:**
- [x] ARP cache with timeout
- [x] ARP request/reply handling
- [x] Packet queuing pending resolution

**Implementation notes:**
- Maintains a small ARP cache with pending vs resolved entries and timeout-based eviction
- Queues one pending packet per unresolved destination and transmits it after resolution
- Exposes ARP table state to userspace for inspection

**Estimate:** 3-4 days

### 4.3 Virtio-net Driver [PARTIAL]
**Status:** Initial implementation landed 2026-03-31  
**Files:** `kernel/driver/virtio.c`, `kernel/driver/virtio_net.c`, `include/virtio.h`  
**Value:** Easiest NIC to test with QEMU  
**Dependencies:** PCI, Virtio core, Ethernet layer  

**Tasks:**
- [x] Basic TX/RX with single-buffer packets
- [x] ifnet integration
- [x] MAC address configuration
- [ ] Link status / advanced feature handling

**Implementation notes:**
- Provides working RX/TX virtqueues and feeds packets into the Ethernet/IP stack
- Good enough for DHCP, DNS, ping, and basic TCP userland testing in QEMU

**Estimate:** 1 week

### 4.3a Real Hardware NIC Drivers [PARTIAL]
**Status:** E1000, PCNET, RTL8111 have full ifnet integration; VMXnet3, netvsc, Intel I219-V, Intel I226-V, and ASIX AX88179 PCI are stubs  
**Files:** `kernel/driver/e1000.c`, `kernel/driver/i219.c`, `kernel/driver/i226.c`, `kernel/driver/ax88179_pci.c`, `kernel/driver/pcnet.c`, `kernel/driver/rtl8111.c`, `kernel/driver/vmxnet3.c`, `kernel/driver/netvsc.c`  
**Value:** Support for real hardware and additional VM platforms  

**E1000 (Intel Gigabit Ethernet) [IMPLEMENTED]:**
- [x] PCI detection (vendor 0x8086, device 0x100E/0x153A)
- [x] BAR0 MMIO mapping
- [x] MAC address from EEPROM/RAL0
- [x] TX/RX descriptor ring setup
- [x] Full ifnet integration (if_output via descriptor ring)
- [x] IRQ handler for TX/RX completion
- [ ] Checksum offload (hardware capable, not wired)
- [ ] Link status change handling

**PCNET (AMD PCNET-PCI II) [IMPLEMENTED]:**
- [x] PCI detection (vendor 0x1022, device 0x2000)
- [x] I/O port-based register access
- [x] 32-bit SWSTYLE mode
- [x] TX/RX descriptor rings
- [x] Initialization block setup
- [x] Full ifnet integration
- [x] IRQ handler for TX/RX completion

**RTL8111 (Realtek Gigabit Ethernet) [IMPLEMENTED]:**
- [x] PCI detection (vendor 0x10EC, device 0x8168/0x8169)
- [x] BAR0 MMIO mapping
- [x] MAC address from registers
- [x] TX/RX descriptor rings (8169-style)
- [x] Full ifnet integration
- [x] IRQ handler for TX/RX completion
- [ ] Jumbo frame support

**VMXnet3 (VMware Paravirtualized) [STUB]:**
- [x] PCI detection (vendor 0x15AD, device 0x07B0)
- [x] BAR mapping (PT and VD registers)
- [x] MAC address reading via command interface
- [ ] TX/RX queue setup
- [ ] Full ifnet integration

**I219-V (Intel Ethernet Connection (7) I219-V) [STUB]:**
- [x] PCI detection for common I219-LM/V IDs (vendor 0x8086)
- [x] BAR0 MMIO mapping and command register enablement
- [x] Basic MAC address read from RAL/RAH and link-state sampling
- [x] BSD-style ifnet attach as `wmX` with stub output callback
- [ ] Descriptor rings, RX/TX datapath, and interrupt-driven completion
- [ ] PHY/MDIO bring-up and e1000e/ICH-specific reset/quirk handling

**I226-V (Intel Ethernet Controller I226-V) [STUB]:**
- [x] PCI detection for common I226 LM/V/IT IDs (vendor 0x8086)
- [x] BAR0 MMIO mapping and command register enablement
- [x] Basic MAC address read from RAL/RAH and link-state sampling
- [x] BSD-style ifnet attach as `igcX` with stub output callback
- [ ] Descriptor rings, RX/TX datapath, and interrupt-driven completion
- [ ] PHY/NVM handling and IGC-family reset/clock/quirk sequencing

**AX88179 (ASIX, PCI-only scaffold) [STUB]:**
- [x] PCI-only probe path scaffold (no xHCI/USB dependency)
- [x] BAR0 MMIO mapping and basic register sampling
- [x] Basic MAC address read from RAL/RAH-style offsets used by the stub
- [x] ifnet attach as `axpX` with stub output callback
- [ ] Validate/lock final PCI IDs for target board implementation
- [ ] Implement RX/TX datapath, interrupts, and ASIX-specific register model

**NetVSC (Hyper-V Synthetic NIC) [STUB]:**
- [x] RNDIS protocol structures defined
- [ ] VMBus infrastructure (blocking dependency)
- [ ] Channel detection and negotiation
- [ ] Full ifnet integration

**Implementation notes:**
- E1000: Most common emulated NIC, works in QEMU/VirtualBox/VMware
- PCNET: Legacy QEMU default NIC, good fallback
- RTL8111: Common on real hardware (laptops, desktops)
- VMXnet3: High-performance VMware option (needs more work)
- NetVSC: Requires VMBus transport layer not yet implemented
- I219-V: Common integrated Intel desktop NIC; attach path exists, datapath still TODO
- I226-V: Intel 2.5GbE path scaffolded as an igc-style attach, datapath still TODO
- AX88179 (PCI): PCI-only scaffold requested; intentionally not tied to USB/xHCI yet

### 4.4 TCP Implementation [PARTIAL]
**Current:** Full state machine with retransmission and graceful teardown  
**Tasks:**
- [x] SYN/SYN-ACK/ACK handshake
- [x] Basic sequence number management
- [x] Retransmission (single-segment, exponential backoff)
- [ ] Flow control (window advertisement exists, not full)
- [x] Connection teardown (FIN/ACK, TIME_WAIT)

**Implementation notes:**
- `tcp_connect()` now emits SYN packets and waits for a SYN-ACK-driven transition to `ESTABLISHED`
- `tcp_input()` handles SYN-ACK completion, ACK-only responses, and basic payload delivery into socket receive buffers
- `telnet` and `netcat` are available as userland smoke tests; terminal synchronization and protocol coverage still need hardening

**Estimate:** 3-4 weeks

### 4.5 Networking Userland [ONGOING]
**Status:** Major userland tooling landed 2026-03-31  
**Files:** `user/ifconfig.c`, `user/route.c`, `user/arp.c`, `user/netinfo.c`, `user/netstat.c`, `user/ping.c`, `user/resolve.c`, `user/nslookup.c`, `user/v6dhcpd.c`, `user/telnet.c`, `user/netcat.c`  

**Delivered:**
- Interface inspection/configuration, route add/delete, ARP inspection, and general network introspection
- Resolver stack, `nslookup`, and DHCP tooling
- Improved `ping` plus basic interactive TCP tools (`telnet`, `netcat`)

---

## Priority Tier 5: Filesystem Enhancements (Weeks 19-24)

### 5.1 Symbolic Links [HIGH] - CORE COMPLETE
**Current:** End-to-end symlink behavior is working for ext2-backed paths, including follow/no-follow semantics and loop-depth enforcement. Slow symlink (>60B) create and `ls -l` display polish remain follow-ups.  
**Files:** `include/stat.h`, `include/vfs.h`, `kernel/fs/vfs.c`, `kernel/core/sysfile.c`, `kernel/fs/vfs_ext2.c`  

**Implementation Plan:**

Phase 1 - Kernel Infrastructure:
- [x] `M_IFLNK` constant in `include/stat.h`
- [x] Add `T_SYMLINK` file type constant (value 4)
- [x] Add `readlink()` and `symlink()` to `struct vnode_ops` in `include/vfs.h`
- [x] Add `SYS_symlink` (syscall 73), `SYS_readlink` (syscall 74), `SYS_lstat` (syscall 75)
- [x] Add `VFS_CAP_SYMLINK` capability flag
- [x] Add `SYMLOOP_MAX` constant (8) for loop detection

Phase 2 - ext2 Support (ext2 natively supports symlinks):
- [x] Add `EXT2_S_IFLNK` and `EXT2_FT_SYMLINK` constants
- [x] Implement `ext2_readlink()` - read target from fast symlink in i_block
- [x] Implement `ext2_symlink()` - create fast symlink (<=60 bytes)
- [ ] Support slow symlink creation (>60 bytes) in ext2
- [x] Wire into ext2 vnode_ops
- [x] Update `ext2_stat()` to properly set `M_IFLNK` mode bits
- [x] Update `ext2_mode_to_type()` to return `T_SYMLINK`

Phase 3 - Path Resolution:
- [x] Modify `ext2_walk()` to detect and follow symlinks during traversal (including intermediate path components)
- [x] Add symlink resolution with configurable follow mode (`vfs_lookup_follow` for follow paths, `vfs_namei` for no-follow syscalls)
- [x] Add symlink loop detection (max 8 levels)
- [x] Keep `lstat()`/`readlink()` on no-follow inode lookup path

Phase 4 - Userspace:
- [x] Add `symlink()`, `readlink()`, `lstat()` wrappers in `user/usys.S`
- [x] Add declarations in `user.h`
- [x] Add `ln -s` support to ln utility
- [ ] Add `ls -l` symlink display support
- [x] Add dedicated symlink regression coverage (`user/symlinktest.c`) including loop and relative-target edge cases

**Definition of done:**
- `symlink("/tmp/target", "/tmp/link")` creates a valid symlink ✅
- `readlink("/tmp/link", buf, size)` returns the target path ✅
- `open("/tmp/link")` follows the symlink to the target ✅
- Symlink chains up to 8 levels work; deeper chains fail at the loop-depth limit ✅
- `lstat()` returns symlink metadata without following ✅

### 5.2 ISO 9660 (CD-ROM) [MEDIUM] - MOSTLY COMPLETE
**Status:** Working read-only implementation landed 2026-03-31  
**Value:** Read ISO images, distribution media, installation CDs  
**Root Support:** Not required - mount only  
**Files:** `kernel/fs/vfs_isofs.c`, `kernel/driver/loop.c`, `user/isotest.c`  

**Completed:**
- [x] Inode-based VFS integration matching the current auxv6 VFS model
- [x] `isofs_mount_init()` reads the primary volume descriptor and root directory record
- [x] 2048-byte ISO sector reads over 512-byte block devices
- [x] Root inode synthesis and pathname traversal
- [x] Directory lookup for `.` / `..` and ordinary entries
- [x] Case-insensitive name matching
- [x] Stripping `;1`-style version suffixes during lookup
- [x] Read-only file I/O and stat support
- [x] Loop-mounted image testing path via `losetup` and `isotest`

**Remaining Work:**
- [ ] Multi-extent file support (`ISO_FLAG_MULTI`)
- [ ] Rock Ridge extensions (PX/NM/SL records for POSIX attrs, long names, symlinks)
- [ ] Joliet support for alternate filename encoding

**Definition of done (basic):** ✅ Achieved
- [x] `mount -t isofs /dev/loop0 /mnt/cdrom` succeeds
- [x] `ls /mnt/cdrom` shows root directory contents
- [x] `cat /mnt/cdrom/FILE.TXT` reads file data correctly
- [x] Path traversal into subdirectories works

**Estimate:** 2-4 more days for follow-up compatibility work, not for basic support

### 5.3 Loop Devices [MEDIUM] - COMPLETE
**Status:** Implemented 2026-03-31; hardened 2026-04-01  
**Value:** Mount ISO and disk images without dedicated hardware  
**Files:** `kernel/driver/loop.c`, `kernel/core/sysfile.c`, `user/losetup.c`, `user/isotest.c`, `user/looptest.c`, `user/mount.c`  

**Completed:**
- [x] `/dev/loop0` through `/dev/loop7` block devices
- [x] Backing regular file support via inode/VFS-backed reads and writes
- [x] `loopsetup()` syscall for attach/configure
- [x] `loopteardown()` syscall for detach
- [x] `loopstatus()` syscall for status queries
- [x] `losetup` userspace utility for list/setup/detach/first-free workflows
- [x] `mount` userspace support for `loopN` device names
- [x] End-to-end ISO test utility (`isotest`)

- [x] Busy-device safety policy — `loop_teardown()` rejects detach while a filesystem is mounted (`vfs_dev_is_mounted()` helper)
- [x] `loop_setup()` input validation: offset alignment, offset < backing size, nblocks range, inode type check
- [x] Extended `loopstatus()` API: returns `offset` and `flags` with `LOOP_STATUS_MOUNTED` bit
- [x] `losetup` list output updated to show offset and mounted columns
- [x] Dedicated regression suite `user/looptest.c`: setup validation, status metadata, and busy-teardown guard groups
- [x] Test ISO (`targetfs/tmp/test.iso`) staged to `/tmp/test.iso` on rootfs for looptest busy-teardown group
- [x] Rootfs image expanded to 256 MB

**Remaining Work:**
- [ ] Richer status reporting (backing pathname)
- [ ] Partition-awareness helpers beyond manual offset/nblocks setup

**Definition of done (current milestone):** ✅ Achieved
- [x] Loop devices register with the block layer
- [x] Backing files can be attached and detached from userspace
- [x] Mounted filesystems can be read through loop devices

### 5.4 Btrfs Read-Only Support [LOW]
**Status:** Not started  
**Value:** Read modern Linux filesystems, data recovery, interop  
**Root Support:** Not required - mount only  
**Files:** `kernel/fs/vfs_btrfs.c` (new)

**Scope:** Read-only access to single-device btrfs volumes. No RAID, no compression, no snapshots.

**Implementation Plan:**

Phase 1 - Superblock and Basics:
- [ ] Create `kernel/fs/vfs_btrfs.c` with VFS registration
- [ ] Define btrfs on-disk structures (superblock, chunk, tree node)
- [ ] Parse superblock at offset 0x10000 (64KB)
- [ ] Validate magic number (0x4D5F53665248425F)
- [ ] Extract root tree location and chunk tree bootstrap

Phase 2 - Chunk Mapping:
- [ ] Parse chunk tree to build logical-to-physical address map
- [ ] Support SINGLE profile only (no RAID)
- [ ] Implement `btrfs_read_logical()` - translate and read

Phase 3 - B-Tree Navigation:
- [ ] Implement B-tree node parsing (internal and leaf nodes)
- [ ] Implement key search within nodes
- [ ] Implement tree traversal for path lookups

Phase 4 - Filesystem Operations:
- [ ] Implement `btrfs_root_inode()` - find FS_TREE root
- [ ] Implement `btrfs_dirlookup()` - search DIR_ITEM/DIR_INDEX
- [ ] Implement `btrfs_read()` - read EXTENT_DATA items
- [ ] Implement `btrfs_stat()` - read INODE_ITEM

**Definition of done (basic):**
- `mount -t btrfs /dev/sda1 /mnt/btrfs` succeeds for simple volume
- `ls /mnt/btrfs` shows root directory
- `cat /mnt/btrfs/file.txt` reads uncompressed file data
- Graceful error on unsupported features (RAID, compression)

**Estimate:** 2-3 weeks

### 5.5 devman - Device Node Manager [MEDIUM]
**Status:** Implemented baseline 2026-04-01  
**Value:** Replaces manual device node creation during boot; establishes centralized `/dev` policy  
**Files:** `user/devman.c`, `targetfs/etc/devman.conf`, `targetfs/etc/rc.S`, `user/init.c`

**Concept:** Userspace utility inspired by mdev/udev that runs in early runlevel (`rc.S`) and creates device nodes in `/dev` from kernel-visible inventory.

**Implemented (current baseline):**
- [x] `devman -s` static scan mode in userspace
- [x] Early-runlevel integration (`rc.S`) replacing init hardcoded node loops
- [x] Dynamic node creation for available block devices and tty/pty endpoints
- [x] Safe idempotent behavior (skip if node already matches expected major/minor)
- [x] Output verbosity tuner via `/etc/devman.conf` (`debug=0/1`)

**Current behavior:**
- On boot, `rc.S` runs `/bin/devman -s` before other setup work.
- Device nodes are created only for detected/known paths in the current model.
- Default output is concise; verbose per-node diagnostics are enabled with `debug=1`.

**Remaining Work / Follow-ups:**
- [ ] Parse richer rule syntax in `/etc/devman.conf` (pattern -> mode/owner/group/action)
- [ ] Add hotplug/event-driven mode (beyond boot-time scan)
- [ ] Add optional stale-node cleanup policy
- [ ] Add kernel-exported inventory/event hooks for full mdev/devfs-style lifecycle

**Definition of done (baseline):** ✅ Achieved
- [x] Boot no longer depends on hardcoded `/dev` node loops in init
- [x] `devman -s` creates required nodes at startup
- [x] Debug verbosity can be tuned in config without rebuilding

### 5.6 NFS Client [MEDIUM-HIGH]
**Status:** Not started - requires RPC/XDR infrastructure  
**Value:** Network filesystem access, diskless boot potential  
**Root Support:** Not required initially - mount only  
**Files:** `kernel/net/xdr.c`, `kernel/net/rpc.c`, `kernel/fs/vfs_nfs.c` (all new)

**Protocol:** NFS v3 over UDP (simpler than TCP, original NFS design)

**Current TCP/Network Stack Assessment:**
| Component | Status | NFS Impact |
|-----------|--------|------------|
| UDP | ✅ Working | Primary transport for NFS v3 |
| TCP | ✅ Basic (single segment) | Limited bandwidth but functional |
| sendto/recvfrom | ✅ Implemented | SYS_sendto=85 / SYS_recvfrom=86; netinet/in.h + arpa/inet.h added |
| XDR library | ❌ Missing | Required for RPC encoding |
| RPC client | ❌ Missing | Required for NFS calls |

**Implementation Plan:**

Phase 1 - XDR Library (~400 lines):
- [ ] Create `kernel/net/xdr.c`
- [ ] `xdr_int()`, `xdr_uint()` - 32-bit integers
- [ ] `xdr_hyper()` - 64-bit integers
- [ ] `xdr_opaque()` - fixed-length opaque data
- [ ] `xdr_bytes()` - variable-length opaque with length
- [ ] `xdr_string()` - null-terminated string
- [ ] `xdr_array()` - variable-length array
- [ ] Encoder and decoder variants

Phase 2 - RPC Client (~500 lines):
- [ ] Create `kernel/net/rpc.c`
- [ ] RPC message header encoding (XID, prog, vers, proc)
- [ ] AUTH_UNIX credential encoding (uid, gid, hostname)
- [ ] RPC reply processing and error handling
- [ ] UDP-based call/reply with retransmit
- [ ] XID tracking for reply matching

Phase 3 - Portmapper Client (~100 lines):
- [ ] Implement GETPORT call to port 111
- [ ] Query NFS program (100003) and MOUNT program (100005)

Phase 4 - Mount Protocol Client (~150 lines):
- [ ] Implement MNT procedure to get root filehandle
- [ ] Store filehandle for NFS operations

Phase 5 - NFS v3 Client (~1000 lines):
- [ ] Create `kernel/fs/vfs_nfs.c` with VFS registration
- [ ] GETATTR - get file attributes
- [ ] LOOKUP - directory entry lookup
- [ ] READ - read file data
- [ ] READDIR/READDIRPLUS - list directory
- [ ] Filehandle-to-inode mapping and caching

Phase 6 - Write Support (Optional):
- [ ] WRITE - write file data
- [ ] CREATE - create file
- [ ] MKDIR - create directory
- [ ] REMOVE - delete file/directory
- [ ] COMMIT - commit writes

**Definition of done (basic read-only):**
- `mount -t nfs server:/export /mnt/nfs` succeeds
- `ls /mnt/nfs` shows remote directory contents
- `cat /mnt/nfs/file.txt` reads remote file data
- Handles network timeouts gracefully

**Dependencies:**
- UDP stack ✅
- XDR library (new)
- RPC client (new)

**Estimate:** 4-6 weeks

---

## Priority Tier 6: POSIX Compliance (Weeks 23-28)

### 6.1 Missing Syscalls [HIGH]
| Syscall | Priority | Complexity | Notes |
|---------|----------|------------|-------|
| ~~lseek~~ | ~~Critical~~ | ~~Low~~ | ✅ Implemented 2026-03-30 |
| ~~dup2~~ | ~~Critical~~ | ~~Low~~ | ✅ Implemented 2026-03-30 |
| ~~fcntl~~ | ~~High~~ | ~~Medium~~ | ✅ Implemented 2026-03-30 |
| select/poll | High | Medium | ✅ Baseline implementation landed 2026-03-31; event semantics and precision timeout behavior can be refined |
| mmap | High | High | Memory mapping |
| ioctl | Medium | Medium | ✅ TTY-focused ioctl syscall support landed 2026-04-01; broader non-tty device ioctl coverage still pending |
| stat/lstat | Medium | Low | Complete stat info |
| time/gettimeofday | Medium | Low | Time support |
| getrlimit/setrlimit | Low | Medium | Resource limits |

### 6.2 Header Compliance [MEDIUM]
**Created / expanded portability headers:**
- `stddef.h` - size_t, NULL, offsetof
- `stdint.h` - uintXX_t, intXX_t
- `sys/types.h` - pid_t, uid_t, off_t, etc.
- `unistd.h` - POSIX constants
- `stdlib.h` - Standard library
- `string.h` - String operations
- `posix/dirent.h` - Directory iteration APIs
- `posix/stdio.h` - Formatting-focused stdio subset
- `posix/stdarg.h`, `posix/setjmp.h`, `posix/ctype.h`, `posix/inttypes.h`, `posix/limits.h`, `posix/paths.h`, `posix/stdbool.h`
- `posix/sys/stat.h`, `posix/sys/time.h`, `posix/sys/times.h`, `posix/sys/ioctl.h`, `posix/sys/param.h`, `posix/sys/resource.h`, `posix/sys/wait.h`

**Still Needed:**
- `netinet/in.h` - Internet addresses
- `arpa/inet.h` - Address conversion
- A more complete user-visible socket API surface around the existing `socket.h`
- Broader stdio completeness and buffering behavior parity for large ports

### 6.3 Library Functions [LOW]
Substantial userspace support now exists in `user/ulib.c` and `user/posix.c`:
- String/memory routines (`memcpy`, `memcmp`, `strstr`, `strtok_r`, etc.)
- Basic stdlib coverage (`strtol`, `strtoul`, `qsort`, `bsearch`, `rand`, environment helpers)
- Formatting wrappers (`vsnprintf`, `snprintf`, `sprintf`, `vsprintf`) and POSIX `dirent` translation

**Current libc compatibility limitations (stdio/regex):**
- stdio buffering is currently minimal (effectively unbuffered behavior for most paths), so throughput and flush semantics do not yet match mature libc implementations.
- `fmemopen()` is currently read-focused for porting use-cases and does not yet provide full writable seek/resize semantics expected by some software.
- stdio state/position APIs (`fseek`/`ftell`/`fgetpos`/`fsetpos`), formatted scanning (`fscanf` family), and advanced error/reporting controls are still incomplete.
- regex engine currently targets practical POSIX BRE/ERE coverage for userland ports (including `REG_EXTENDED`, `REG_ICASE`, basic classes, alternation, and repetition) but does not yet provide full POSIX match-subexpression reporting parity.
- regex performance is currently oriented toward correctness and portability over advanced optimization; large-pattern or pathological cases may require future tuning.

**Portability checklist (stdio/regex API status):**

| API / Capability | Status | Notes |
|---|---|---|
| `fopen` / `fdopen` / `fclose` | Implemented | Baseline stream open/close over auxv6 fds. |
| `ferror` / `feof` / `clearerr` / `fflush` | Implemented | Basic status reporting; `fflush` is currently a no-op in minimal stream model. |
| `fgetc` / `getc` / `ungetc` / `fputc` / `putc` | Implemented | Single-byte stream I/O available. |
| `fgets` / `fputs` / `puts` | Implemented | Line/string primitives available for common ports. |
| `fread` / `fwrite` | Implemented | Basic block I/O over stream wrapper. |
| `getline` / `getdelim` | Implemented | Dynamic line growth supported. |
| `fmemopen` | Partial | Read-focused implementation; writable/seek-rich semantics pending. |
| `fprintf` / `vfprintf` / `printf` macro path | Implemented | Provided via stream layer with auxv6-compatible mapping. |
| `vprintf` macro path | Implemented | Mapped to `vfprintf(stdout, ...)`. |
| `perror` | Partial | Available with errno-based output; formatting parity can be improved. |
| `fseek` / `ftell` / `rewind` | Missing | Needed by some larger third-party ports. |
| `fscanf` / `vfscanf` family | Missing | Scanning APIs not yet part of compatibility layer. |
| `setvbuf` / `setbuf` / `tmpfile` | Missing | Full buffering/temp-stream utilities pending. |
| `regcomp` / `regexec` / `regerror` / `regfree` | Implemented | Userspace regex engine integrated into libc layer. |
| `REG_EXTENDED` / `REG_ICASE` / `REG_NOSUB` | Implemented | Supported flags for current port set. |
| Character classes and bracket ranges | Implemented | Basic bracket/range handling present. |
| BRE escaped word boundaries (`\\<`, `\\>`) | Implemented | Added for grep-style word matching paths. |
| Subexpression captures and full POSIX `pmatch` parity | Partial | Core matching works; full capture parity remains future work. |
| Regex optimization (DFA/NFA tuning) | Partial | Correctness-first implementation; optimization backlog tracked. |

**Still Needed:**
- Wider `FILE *` conformance surface (additional stdio routines and edge-case compatibility)
- More complete socket-family and networking headers
- Additional portability wrappers for larger third-party ports

### 6.4 POSIX Porting And Init [ONGOING]
**Status:** Significant userland progress landed through 2026-04-01  
**Files:** `user/posix.c`, `user/ulib.c`, `user/setjmp.S`, `user/init.c`, `user/runlevel.c`, `user/telinit.c`, `kernel/core/exec.c`  

**Delivered:**
- Enough libc/POSIX scaffolding to experiment with ported software such as `dash`
- `exec()` shebang support for interpreter scripts
- SysV-style init flow with `/etc/rc.d/rc.S`, runlevel transitions, and `telinit`/`runlevel` tooling
- `man` userspace utility and an initial manpage corpus, with generation/update workflow in `docs/man-pages.md`
- More POSIX-like `kill(pid, sig)` behavior wired into signal delivery
- Linux-compatible terminal ioctl numbering for common termios and queue-state operations
- Dynamic PTY integration with `openpty()`/`ptsname_r()` and `/dev/ptmx` -> `/dev/pts/N` allocation
- `termcheck` coverage expanded to include concurrent multi-PTY shell isolation/lifecycle and max create/terminate stress behavior
- Targetfs terminal defaults (`/etc/termcap`, `TERM=vt100`, `TERMCAP=/etc/termcap`) for better interactive app behavior
- New core diagnostic utilities in base userland: `which`, `lsof` (via `/proc/lsof`), and baseline magic/signature-based `file`
- Matching manpage additions for `which`, `lsof`, and `file`
- Upstream-style grep porting path: `ports/sbase/Makefile.auxv6` builds `ports/sbase/grep.c` against auxv6 libc compatibility shims and stages it as `/bin/sgrep`

---

## Key Infrastructure Added

### Drivers
| File | Description |
|------|-------------|
| `kernel/driver/pci.c` | PCI bus enumeration, BAR decode, mapping, and helper APIs |
| `kernel/driver/virtio.c` | Virtio framework core |
| `kernel/driver/virtio_net.c` | Initial virtio network driver with RX/TX integration |
| `kernel/driver/virtio_blk.c` | Initial virtio block driver with blockdev integration |
| `kernel/driver/e1000.c` | Intel E1000 Gigabit Ethernet with full ifnet integration |
| `kernel/driver/i219.c` | Intel I219-V/e1000e-style attach stub (PCI match, MMIO map, MAC/link read, ifnet registration) |
| `kernel/driver/i226.c` | Intel I226-V/igc-style attach stub (PCI match, MMIO map, MAC/link read, ifnet registration) |
| `kernel/driver/ax88179_pci.c` | ASIX AX88179 PCI-only stub scaffold (no xHCI/USB dependency) |
| `kernel/driver/pcnet.c` | AMD PCNET-PCI II with full ifnet integration |
| `kernel/driver/rtl8111.c` | Realtek RTL8111/8168 Gigabit Ethernet with full ifnet integration |
| `kernel/driver/vmxnet3.c` | VMware VMXnet3 paravirtualized NIC stub |
| `kernel/driver/netvsc.c` | Microsoft Hyper-V NetVSC paravirtualized NIC stub |
| `kernel/driver/pty.c` | Dynamic PTY driver backend with multi-slot allocation, per-endpoint state, termios/winsize/ioctl support, and PTY poll/readiness semantics |
| `kernel/driver/ahci.c` | AHCI/SATA driver with polling DMA read/write |
| `kernel/driver/nvme.c` | NVMe driver with I/O queue and basic RW path |
| `kernel/driver/loop.c` | Loop block device driver; setup validation, busy-teardown guard, extended status API |
| `kernel/driver/console.c` | `cprintf` expanded: width, precision (`%.*s`/`%.N`), flags (`-`/`0`), `%c`, `%o`, `%i`, length modifier |

### Headers
| File | Description |
|------|-------------|
| `include/pci.h` | PCI definitions |
| `include/virtio.h` | Virtio definitions |
| `include/stddef.h` | Standard definitions |
| `include/stdint.h` | Integer types |
| `include/stdlib.h` | Standard library |
| `include/string.h` | String operations |
| `include/strings.h` | BSD strings compatibility shim |
| `include/stdio.h` | Baseline `FILE *` stdio abstraction for ports |
| `include/regex.h` | POSIX-style regex API for userspace |
| `include/unistd.h` | POSIX constants |
| `include/sys/types.h` | POSIX types |
| `include/posix/*` | Portability headers for userland ports |

### Network
| File | Description |
|------|-------------|
| `kernel/net/ethernet.c` | Ethernet framing, padding, and protocol demux |
| `kernel/net/arp.c` | ARP cache, request/reply handling, and pending-packet resolution |

### Userland
| File | Description |
|------|-------------|
| `user/resolve.c` | DNS / hostname resolver support |
| `user/nslookup.c` | Name resolution utility |
| `user/v6dhcpd.c` | DHCP tooling |
| `user/telnet.c` | Basic Telnet client |
| `user/netcat.c` | Basic TCP/UDP client/server utility |
| `user/losetup.c` | Loop device list/setup/detach utility; offset and mounted-flag columns added |
| `user/isotest.c` | ISO 9660 and loop-device smoke test utility |
| `user/looptest.c` | Loop device regression suite: setup validation, status metadata, and busy-teardown guard |
| `user/which.c` | PATH-aware executable lookup utility |
| `user/lsof.c` | Open-file inspection utility backed by `/proc/lsof` |
| `user/file.c` | Baseline file-type detector using signatures + lightweight heuristics |
| `user/stdio.c` | Baseline userspace `FILE *` stdio implementation for ports |
| `user/regex.c` | Userspace regular-expression engine (`regcomp`/`regexec` family) |
| `user/calloc.c` | `calloc()` libc helper used by ported software |
| `user/posix.c` | POSIX compatibility wrappers |
| `user/termcheck.c` | Terminal/PTY/ioctl compatibility regression checks, multi-PTY shell isolation/lifecycle, and PTY allocation stress tests |
| `user/runlevel.c` | Current/previous runlevel reporting |
| `user/telinit.c` | Runlevel transition requests |
| `user/man.c` | `man` command for in-system manual page viewing |
| `tools/gen-man-pages.sh` | Helper script for generating/updating manual pages |

### Filesystem
| File | Description |
|------|-------------|
| `kernel/fs/vfs_isofs.c` | ISO 9660 read-only filesystem with current VFS integration |
| `kernel/fs/vfs_btrfs.c` | Btrfs read-only support (planned) |
| `kernel/fs/vfs_nfs.c` | NFS v3 client (planned) |
| `kernel/net/xdr.c` | XDR encoding/decoding for RPC (planned) |
| `kernel/net/rpc.c` | ONC RPC client (planned) |
| `user/devman.c` | Device node manager utility with boot-time static scan mode (`devman -s`) |

---

## Estimated Timeline

| Phase | Duration | Focus |
|-------|----------|-------|
| Foundation | 4 weeks | Signal delivery, critical syscalls |
| Device Infra | 4 weeks | PCI, interrupts, DMA |
| Storage | 4 weeks | Virtio-blk polish, then AHCI |
| Networking | 6 weeks | TCP hardening, virtio-net polish, real NIC support |
| Filesystems | 4 weeks | Symlinks, ISO9660 |
| POSIX | 6 weeks | Missing syscalls, porting headers, libc completeness |
| **Total** | **~28 weeks** | |

Several items have already landed out of order relative to this original plan, notably virtio-blk, Ethernet/ARP, networking userland, and substantial POSIX portability work.

---

## Quick Wins (Can be done anytime)

1. **Expand procfs further** - Add `/proc/meminfo` and richer process/system nodes - 2 hours for basic files
2. **Implement gettimeofday syscall** - 2 hours
3. **Back real getrlimit/setrlimit syscalls behind the existing header stubs** - 1 hour
4. **Add `netinet/in.h` and `arpa/inet.h` compatibility headers, then flesh out userspace socket declarations** - 2-3 hours
5. **Polish virtio-net link state / diagnostics** - 2 hours
6. **Expand manpage coverage for key networking/storage/admin tools** - 2-4 hours

---

## Testing Infrastructure Needed

1. **Unit test framework** for kernel components
2. **QEMU scripting** for automated boot tests
3. **POSIX conformance test suite** (subset)
4. **Network test environment** with virtual bridge

---

## Next Tranche Plan (2026-04-02 to 2026-04-16)

Primary goal: convert recently landed features into a more reliable baseline while unblocking NFS and broader POSIX ports.

### Tranche A - Storage reliability hardening
- Virtio-blk: discard/write-zeroes helpers now exposed via `/proc/vblk_flush` admin commands (`discard`, `discard_all`, `write_zeroes`, `write_zeroes_all`) and deterministic unsupported behavior can be forced with `force_no_discard` / `force_no_write_zeroes`.
- Virtio-blk: add error accounting + bounded retry policy for transient I/O failures.
- AHCI: complete mount/unmount endurance loop with timeout/recover telemetry and no controller lockups.
- NVMe: add command timeout handling with controller reset-on-fatal fallback.

**Definition of done:**
- `lsblk`/mount behavior remains stable across repeated attach/mount/unmount cycles on virtio-blk, AHCI, and NVMe.
- Unsupported storage operations fail predictably (no silent success, no panic).
- Timeout/error counters are visible through existing diagnostic paths.

### Tranche B - NFS prerequisite syscall and headers ✅ COMPLETE
- `sendto()` and `recvfrom()` implemented as SYS_sendto=85 / SYS_recvfrom=86.
  - Auto-bind ephemeral source port on first unbound `sendto`.
  - `flags` must be 0; SOCK_DGRAM and SOCK_RAW supported; SOCK_STREAM returns -1.
  - NULL destination falls back to `s->remote_addr` (connected-socket path).
  - NULL src/srclen in `recvfrom` is safe (addr write-back is conditional).
- `include/netinet/in.h` and `include/arpa/inet.h` added.
- `inet_aton`, `inet_addr`, `inet_ntoa`, `inet_pton`, `inet_ntop` in `user/ulib.c`.
- `user/udptest.c` regression test: auto-bind, round-trip + NULL-src, connected-dst.

**Definition of done:**
- ✅ UDP echo-style userspace tests written using `sendto`/`recvfrom`.
- ✅ A minimal RPC-like datagram exchange can be expressed in userspace without ad-hoc prototypes.

### Tranche C - devman phase-2 policy improvements
- Add richer `/etc/devman.conf` rule parsing (path pattern -> mode/owner/group/action).
- Add optional stale-node cleanup mode for boot-time reconciliation.
- Keep hotplug/event mode out-of-scope for this tranche, but define kernel/userspace interface requirements.

**Definition of done:**
- Policy-driven node mode/ownership works in boot scan mode.
- Cleanup mode is opt-in and safe against active device nodes.
- Hotplug interface proposal is documented with concrete data structures and event semantics.

### Tranche D - observability and userland ergonomics
- Expand procfs coverage where low-risk and high-value (`/proc/lsof` formatting polish, optional per-process filtering strategy notes).
- Continue manpage expansion for recently landed storage/network/admin commands.
- Add scripted smoke checks for `which`, `lsof`, and `file` in the userland regression flow.

**Definition of done:**
- New utilities are covered by scripted smoke tests in QEMU boot runs.
- Manpages exist for each tool promoted to default userland in this tranche.

---

## Next Steps (Recommended Order)

1. **Execute Tranche A (storage reliability hardening)** to reduce corruption/lockup risk before larger feature work.
2. ~~**Execute Tranche B (sendto/recvfrom + networking headers)**~~ ✅ **Complete** — sendto/recvfrom landed; netinet/in.h + arpa/inet.h + udptest wired.
3. **Execute Tranche C (devman policy parsing + optional cleanup)** to strengthen `/dev` lifecycle safety.
4. **Execute Tranche D (observability + manpages + utility smoke tests)** to lock in operational confidence.
5. **Then begin XDR/RPC implementation** (`xdr.c`, `rpc.c`) followed by NFS read-only mount path.
6. **After NFS foundation:** return to Btrfs read-only and devman hotplug/event lifecycle enhancements.
