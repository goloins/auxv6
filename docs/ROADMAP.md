# auxv6 Modernization Roadmap

## Project Overview

auxv6 is an xv6-derived Unix-like operating system with significant enhancements including:
- Multi-backend VFS layer (xv6fs, ext2, msdosfs, procfs, tmpfs)
- BSD-style networking with TCP/IP stack
- Signal handling infrastructure
- Job control with process groups, sessions, terminal control
- Multi-filesystem support with mount table

**Architecture:** x86 32-bit, single address space per process  
**Current State:** OS with working core components, ext2-root boot as the default path, and a growing POSIX-style userland, but still missing several modern drivers and full POSIX compliance

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
| Virtio storage | 95% | Virtio core + virtio-blk with DoD checklist complete; queue-depth tuning and retry telemetry in place |

### ⚠️ Partially Implemented (50-74%)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| Networking interfaces | 60% | BSD ifnet abstraction, loopback, virtio-net, routing, DHCP tooling, outbound packet path |
| POSIX compatibility layer | 70% | Broader tty/ioctl compatibility, dynamic `openpty`/`ptsname_r` path, dash portability fixes; many APIs still stubbed or partial |
| Userland docs/manpages | 72% | `man` utility plus baseline pages are available; coverage and completeness still growing |
| Graphics / framebuffer console | 70% | Framebuffer core, display registry, builtin font/render path, rich `/proc/gfxstats`, virtio-gpu scanout discovery, display-sized framebuffer allocation, display-derived readable boot geometry, and a manually validated stable virtio-gpu mirror path are landed; the normal console path is still CGA-authoritative, virtio-gpu still uses whole-frame uploads, and no `/dev/fb0` or `/dev/dri/card0` userspace ABI exists yet |
| procfs | 75% | `/proc/uptime`, `/proc/version`, `/proc/pci`, `/proc/vblk_flush`, `/proc/ahci_tune`, `/proc/meminfo`, `/proc/ps`, `/proc/mountstats`, `/proc/gfxstats`, `/proc/lsof`; breadth improved but still sparse overall |
| Real NICs | 65% | E1000, PCNET, RTL8111 have full ifnet integration; VMXnet3 has a basic polling datapath; Hyper-V netvsc, Intel I219-V, Intel I226-V, and ASIX AX88179 PCI remain stubs |
| Device node management | 70% | `devman -s` creates `/dev` nodes at early runlevel from kernel-visible inventory; hotplug/event mode and richer policy rules still pending |
| Modern storage | 70% | AHCI interrupt-driven completions with slot allocator and ATAPI read-only; NVMe basic RW path in place; timeout recovery still pending |

### 🚧 Early Or Stubbed (0-49%)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| Btrfs | None | Planned read-only support |
| NFS | None | Planned; requires XDR/RPC infrastructure |
| Device hotplug/eventing | None | Planned kernel event path for live node add/remove beyond boot-time `devman -s` |

---

## Libc / POSIX Portability Target

- Primary userland target: shell/coreutils/findutils-grade POSIX software.
- Locale target: C locale only.
- The immediate libc priority is a truthful and useful public surface, not a
  larger set of aspirational declarations.
- Threading is now an explicit requirement, but it is tracked as a separate
  kernel-plus-libc stream so smaller portability wins are not blocked on the
  thread design itself.

### Planned Tranches

1. Tranche 1: truthful surface and low-kernel-dependency portability helpers.
  Promote wrapper-only canonical headers and implement userspace-only helpers
  such as `system`, `realpath`, `mk*temp`, `getlogin_r`, `pause`,
  `confstr`/`pathconf`, and PTY POSIX wrappers.
2. Tranche 2: time and stream correctness. Replace stub time surfaces and add
  stdio positioning, scanning, and basic truthful stream-buffering APIs that
  portable tools expect. This tranche is now basically closed.
3. Tranche 3: shell, text, and find surfaces. Add `fnmatch`, `glob`,
  `scandir`/`alphasort`, one tree-walk API (`nftw` or `fts`), and minimal
  C-locale plumbing.
4. Tranche 4: identity and portability polish. Add `pwd.h`/`grp.h` lookups and
  clean up any declarations that still do not meet the target profile.
5. Parallel thread-enablement track: replace placeholder `pthread_*` types with
  a real shared-address-space thread model, TLS-ready libc state, and a
  minimal pthread subset once the kernel/runtime pieces exist.

---

## Active Plan (2026-04)

Primary goal: convert recently landed features into a more reliable baseline while unblocking NFS and broader POSIX ports.

### Tranche A - Storage reliability hardening
- Virtio-blk: tighten error accounting + bounded retry policy for transient I/O failures.
- AHCI: finish mount/unmount endurance loops with timeout/recover telemetry and no controller lockups.
- NVMe: add command timeout handling with controller reset-on-fatal fallback.

**Definition of done:**
- `lsblk`/mount behavior remains stable across repeated attach/mount/unmount cycles on virtio-blk, AHCI, and NVMe.
- Unsupported storage operations fail predictably (no silent success, no panic).
- Timeout/error counters are visible through existing diagnostic paths.

### Tranche B - devman phase-2 policy improvements
- Add richer `/etc/devman.conf` rule parsing (path pattern -> mode/owner/group/action).
- Add optional stale-node cleanup mode for boot-time reconciliation.
- Keep hotplug/event mode out-of-scope for this tranche, but define kernel/userspace interface requirements.

**Definition of done:**
- Policy-driven node mode/ownership works in boot scan mode.
- Cleanup mode is opt-in and safe against active device nodes.
- Hotplug interface proposal is documented with concrete data structures and event semantics.

### Tranche C - observability and userland ergonomics
- Expand procfs coverage where low-risk and high-value (`/proc/lsof` formatting polish, per-process filtering strategy notes).
- Continue manpage expansion for recently landed storage/network/admin commands.
- Add scripted smoke checks for `which`, `lsof`, and `file` in the userland regression flow.

**Definition of done:**
- New utilities are covered by scripted smoke tests in QEMU boot runs.
- Manpages exist for each tool promoted to default userland in this tranche.

### Tranche D - NFS foundation
- Implement XDR (`kernel/net/xdr.c`) and RPC client (`kernel/net/rpc.c`).
- Implement portmapper probe and minimal MOUNT protocol path.
- Land a read-only NFS v3 mount path as the first filesystem client milestone.

**Current status (2026-04-02):**
- XDR + RPC codec path landed in kernel.
- Kernel-internal UDP RPC transport is integrated via socket layer helpers
  (`ksock_open_udp`, `ksock_sendto`, `ksock_recvfrom_timeout`) and `rpc_call()` now
  performs real UDP send/receive with timeout and RPC reply validation.
- MOUNT portmapper (`PMAPPROC_GETPORT`) and MOUNT/UMOUNT requests now run over the
  integrated RPC UDP path.
- NFSv3 procedures (`GETATTR`, `LOOKUP`, `READ`, `READDIR`) now execute RPC requests
  over UDP through the shared RPC transport.
- VFS NFS backend (`kernel/fs/vfs_nfs.c`) is wired as a read-only filesystem type,
  with mount-time source parsing (`server:/export`), root handle acquisition, and
  basic `namei`/`dirlookup`/`read`/`stat` vnode plumbing.
- `mount` userspace now passes NFS source strings to `mount(2)` so
  `mount server:/share nfs /mnt/nfs` maps into kernel mount data.
- Detailed notes: `docs/nfs-v3-integration.md`.

**Definition of done:**
- `mount -t nfs server:/export /mnt/nfs` succeeds in basic read-only tests.
- `ls /mnt/nfs` and `cat /mnt/nfs/file.txt` work on a simple export.

---

## Next Steps (Recommended Order)

1. **Execute Tranche A (storage reliability hardening)** to reduce corruption/lockup risk before larger feature work.
2. **Execute Tranche B (devman policy parsing + optional cleanup)** to strengthen `/dev` lifecycle safety.
3. **Execute Tranche C (observability + manpages + utility smoke tests)** to lock in operational confidence.
4. **Begin Tranche D (XDR/RPC + NFS read-only mount)** once storage reliability is stable.
5. **After NFS foundation:** return to Btrfs read-only and devman hotplug/event lifecycle enhancements.

---

## Remaining POSIX/Libc Gaps (Selected)

- Missing syscalls: `mmap`, `getrlimit`/`setrlimit`; broader `ioctl` coverage for non-tty devices.
- libc: `fscanf`/`vfscanf` family, `tmpfile`, writable `fmemopen` semantics, and fuller `perror` parity.
- Headers: `netdb.h` plus fuller socket-family declarations and constants for larger ports.

---

## Quick Wins (Can be done anytime)

1. **Expand procfs further** - add `procfs` summaries like `/proc/net`, `/proc/sockets`, or per-process fd filters.
2. **Back real getrlimit/setrlimit syscalls behind the existing header stubs** - 1 hour.
3. **Polish virtio-net link state / diagnostics** - 2 hours.
4. **Expand manpage coverage for key networking/storage/admin tools** - 2-4 hours.
5. **Add a small `lsblk`/`mount` smoke test** for multiple devices (virtio-blk + AHCI + NVMe) - 2-4 hours.

---

## Testing And Tooling Gaps

1. **Kernel unit test framework** for small, deterministic components.
2. **Guest automation expansion** beyond the current expect harness (broader smoke suites and stress coverage).
3. **POSIX conformance test subset** aligned with the tranche plan.
4. **Network test environment** with virtual bridge + DHCP/DNS fixtures.

---

## Completed

### Priority Tier 1: Foundation (Weeks 1-4) [COMPLETE]

These items were blocking everything else and are now done.

#### 1.1 Signal Delivery to Userspace [COMPLETE]
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

#### 1.2 lseek Syscall [COMPLETE]
**Status:** Implemented 2026-03-30  
**Files:** `kernel/core/sysfile.c`, `include/syscall.h`, `include/fcntl.h`, `user/usys.S`  
**Implementation:**
- Added `SYS_lseek` (syscall 66) supporting SEEK_SET, SEEK_CUR, SEEK_END
- Returns new offset on success, -1 on failure
- Cannot seek on pipes or sockets (returns -1)
- Validates for negative resulting offsets

#### 1.3 dup2 Syscall [COMPLETE]
**Status:** Implemented 2026-03-30  
**Files:** `kernel/core/sysfile.c`, `include/syscall.h`, `user/usys.S`  
**Implementation:**
- Added `SYS_dup2` (syscall 67) to duplicate fd to specific number
- Closes newfd if already open (POSIX behavior)
- Returns newfd on success, handles oldfd==newfd case correctly

#### 1.4 fcntl Syscall [COMPLETE]
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

### Priority Tier 2: Device Infrastructure (Weeks 5-8) [COMPLETE]

#### 2.1 PCI Subsystem [COMPLETE]
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

#### 2.2 Interrupt Routing Modernization [COMPLETE]
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

#### 2.3 DMA Abstraction [COMPLETE]
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

### Storage Milestones (Completed)

#### 3.1 Virtio-blk Driver [COMPLETE]
**Status:** DoD checklist complete; multi-disk regression + mount/persist stress passed.  
**Files:** `kernel/driver/virtio.c`, `kernel/driver/virtio_blk.c`, `include/virtio.h`, `user/lsblk.c`, `user/mount.c`, `user/init.c`  
**Definition of done:**
- [x] Multiple virtio disks can be attached and independently read/written/mounted
- [x] No hardcoded device-0 behavior remains in I/O and capacity paths
- [x] Flush/discard/write-zeroes are feature-gated and return deterministic errors when unsupported
- [x] Stress pass: repeated mount/write/umount/remount cycles complete without data corruption (`make test-virtioblk-mount-stress`)

#### 5.3 Loop Devices [COMPLETE]
**Status:** Implemented 2026-03-31; hardened 2026-04-01  
**Files:** `kernel/driver/loop.c`, `kernel/core/sysfile.c`, `user/losetup.c`, `user/isotest.c`, `user/looptest.c`, `user/mount.c`  
**Definition of done (current milestone):**
- [x] Loop devices register with the block layer
- [x] Backing files can be attached and detached from userspace
- [x] Mounted filesystems can be read through loop devices

---

### Network Stack (Completed Areas)

#### 4.1 Ethernet Layer [COMPLETE]
**Status:** Implemented 2026-03-31  
**Files:** `kernel/net/ethernet.c`, `kernel/net/device.c`, `include/net.h`  
**Tasks:**
- [x] Frame encapsulation/decapsulation
- [x] MTU handling
- [x] Protocol demux (ETHERTYPE_IP, ETHERTYPE_ARP)

#### 4.2 ARP Implementation [COMPLETE]
**Status:** Implemented 2026-03-31  
**Files:** `kernel/net/arp.c`, `user/arp.c`  
**Tasks:**
- [x] ARP cache with timeout
- [x] ARP request/reply handling
- [x] Packet queuing pending resolution

---

### POSIX / Libc Milestones (Completed)

#### Tranche 1: portability helpers and canonical headers [COMPLETE]
- Canonical `limits.h`, `inttypes.h`, `setjmp.h`, `sys/resource.h` landed; `include/posix/*` now forward.
- New runtime helpers: configuration/login/sleep helpers, `realpath`, `mk*temp` family.
- `user/posix.c` adds `execl`, `execlp`, `system`; `user/tty.c` adds `posix_openpt`, `grantpt`, `unlockpt`.

#### Tranche 2: time + stdio correctness [COMPLETE]
- `time.h` + `sys/time.h` fixed; `user/timecore.c` provides `gettimeofday`, `clock_gettime`, `clock_getres`, `clock_nanosleep`, `nanosleep`, `time`, `gmtime`/`localtime`, `mktime`, `strftime`, with `CLOCK_MONOTONIC` now backed by a kernel monotonic clock source.
- `include/stdio.h` gains buffering/position/scan APIs; `user/stdio.c` implements seek/tell and line/scan support.
- Stream buffering (`setvbuf`, `setbuf`, `setlinebuf`) now works for fd-backed output streams.

#### ABI cleanup phase 5 [COMPLETE]
- `_start` entry in `user/crt0.S`; `exit()` is now a real libc symbol.
- `printf` split into `dprintf`/`vdprintf` and `printf`/`vprintf` streams.
- Header surface normalized to canonical POSIX spellings where ABI already matches.

#### NFS prerequisites (sendto/recvfrom + headers) [COMPLETE]
- `sendto()` and `recvfrom()` implemented (SYS_sendto=85 / SYS_recvfrom=86).
- `include/netinet/in.h` and `include/arpa/inet.h` landed.
- `inet_aton`, `inet_addr`, `inet_ntoa`, `inet_pton`, `inet_ntop` in `user/ulib.c`.
- `user/udptest.c` regression covers UDP round-trip and null-src cases.

---

### Key Infrastructure Added (Completed)

#### Drivers
| File | Description |
|------|-------------|
| `kernel/driver/pci.c` | PCI bus enumeration, BAR decode, mapping, and helper APIs |
| `kernel/driver/virtio.c` | Virtio framework core |
| `kernel/driver/virtio_net.c` | Initial virtio network driver with RX/TX integration |
| `kernel/driver/virtio_blk.c` | Virtio block driver with blockdev integration |
| `kernel/driver/e1000.c` | Intel E1000 Gigabit Ethernet with full ifnet integration |
| `kernel/driver/i219.c` | Intel I219-V/e1000e-style attach stub (PCI match, MMIO map, MAC/link read, ifnet registration) |
| `kernel/driver/i226.c` | Intel I226-V/igc-style attach stub (PCI match, MMIO map, MAC/link read, ifnet registration) |
| `kernel/driver/ax88179_pci.c` | ASIX AX88179 PCI-only stub scaffold (no xHCI/USB dependency) |
| `kernel/driver/pcnet.c` | AMD PCNET-PCI II with full ifnet integration |
| `kernel/driver/rtl8111.c` | Realtek RTL8111/8168 Gigabit Ethernet with full ifnet integration |
| `kernel/driver/vmxnet3.c` | VMware VMXnet3 paravirtualized NIC stub |
| `kernel/driver/netvsc.c` | Microsoft Hyper-V NetVSC paravirtualized NIC stub |
| `kernel/driver/pty.c` | Dynamic PTY driver backend with multi-slot allocation, per-endpoint state, termios/winsize/ioctl support |
| `kernel/driver/ahci.c` | AHCI/SATA driver with interrupt-driven DMA read/write and ATAPI read-only path |
| `kernel/driver/nvme.c` | NVMe driver with I/O queue and basic RW path |
| `kernel/driver/loop.c` | Loop block device driver; setup validation, busy-teardown guard, extended status API |
| `kernel/driver/console.c` | `cprintf` expanded: width, precision, flags, `%c`, `%o`, `%i`, length modifier |

#### Headers
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
| `include/netinet/in.h` | Internet address definitions |
| `include/arpa/inet.h` | Address conversion helpers |
| `include/posix/*` | Portability headers for userland ports |

#### Network
| File | Description |
|------|-------------|
| `kernel/net/ethernet.c` | Ethernet framing, padding, and protocol demux |
| `kernel/net/arp.c` | ARP cache, request/reply handling, and pending-packet resolution |

#### Userland
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
| `user/ping.c` | ICMP echo utility; runs until ^C, prints RTT statistics on exit |
| `user/traceroute.c` | Route tracing via ICMP ECHO probes with increasing TTL |
| `user/stdio.c` | Baseline userspace `FILE *` stdio implementation for ports |
| `user/regex.c` | Userspace regular-expression engine (`regcomp`/`regexec` family) |
| `user/calloc.c` | `calloc()` libc helper used by ported software |
| `user/posix.c` | POSIX compatibility wrappers |
| `user/termcheck.c` | Terminal/PTY/ioctl compatibility regression checks |
| `user/runlevel.c` | Current/previous runlevel reporting |
| `user/telinit.c` | Runlevel transition requests |
| `user/man.c` | `man` command for in-system manual page viewing |
| `tools/gen-man-pages.sh` | Helper script for generating/updating manual pages |

#### Filesystem
| File | Description |
|------|-------------|
| `kernel/fs/vfs_isofs.c` | ISO 9660 read-only filesystem with current VFS integration |
| `user/devman.c` | Device node manager utility with boot-time static scan mode (`devman -s`) |

---

## Past Changes (2026-03-30 to 2026-04-03)

- `ping` revised to run continuously until ^C (SIGINT), printing standard statistics on exit.
- `traceroute` added: ICMP ECHO probe with increasing TTL, `setsockopt(IP_TTL)` support, three probes per hop, 1 s timeout per probe.
  - **FIXME:** intermediate hops are invisible under QEMU SLIRP; SLIRP does not synthesise ICMP TIME_EXCEEDED per hop, so every destination shows at hop 1. Needs tap/bridge networking or a SLIRP-aware workaround to show real path depth.
- `setsockopt`/`getsockopt` syscalls added (SYS_setsockopt=88, SYS_getsockopt=89) with `IPPROTO_IP`/`IP_TTL` support.
- `ip_output_ttl()` added to IP layer; raw socket send paths thread per-socket TTL through to outgoing packets.
- `ICMP_TIMXCEED`, `ICMP_UNREACH`, and related ICMP type/code constants added to `include/net.h`.
- AHCI recovery/retry work landed (idle-stall recovery, bounded fault injection, mount-stress + soak harnesses, `test-ahci-regression`).
- AHCI interrupt-driven completion and multi-slot queue depth enabled; ATAPI read-only path added with `/dev/cdrom*` nodes.
- Toolchain hardening added (`-nostdinc`, toolchain checks for 32-bit libgcc helpers).
- libc portability tranches 1 and 2 landed: canonical headers, time/civil calendar, stdio seek/tell/scan, and stream buffering (`setvbuf`/`setbuf`/`setlinebuf`).
- libc ABI cleanup phase 5 completed (`crt0.S`, clean `exit`, `dprintf` vs `printf` split).
- procfs expanded (`/proc/ahci_tune`, `/proc/vblk_flush`, `/proc/meminfo`, `/proc/lsof`).
- VFS/getdents system-root iteration now suppresses backend `.` and `..` entries only at `/`; other mount roots such as `/tmp` are unchanged.
- Guest harness improved with command-completion markers and safer ANSI handling.
- devman integrated into boot runlevel; `man` and initial manpage set added.
- Virtio-blk stress and retry harnesses added; virtio multi-device probe fixes landed.
- Loop device hardening and `looptest` regression suite landed.
- Terminal/PTY compatibility tranches landed (query/reply support, job control, termcap upgrades, termcheck smoke/full suites).
