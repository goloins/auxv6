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

## Recent Progress (2026-03-30 to 2026-04-01)

- Signal delivery, `alarm()`, `SIGPIPE`, `lseek`, `dup2`, and baseline `fcntl()` support landed and are now integrated into the main syscall path.
- PCI enumeration, IRQ registration, DMA allocation helpers, and `lspci` landed as the Tier 2 device foundation.
- ext2 is now the default root filesystem build target, staged images are created with correct `root:root` ownership, and init is executed from the mounted root filesystem after VFS initialization.
- Virtio infrastructure moved from scaffolding to working code: `virtio-blk` now probes, negotiates features, performs block I/O, registers with the block layer, and shows up through `lsblk` and `/dev/vd*` nodes.
- The network stack moved beyond loopback-only behavior: Ethernet framing, ARP cache/request/reply, routing controls, virtio-net RX/TX, DHCP tooling, resolver/`nslookup`, and outbound internet ping all landed.
- TCP now exchanges real packets with a basic three-way handshake and ACKed payload delivery; `telnet` and `netcat` were added as rough but functional userland validation tools.
- POSIX porting work expanded substantially: new `include/posix/*` headers, broader libc-style helpers in `user/ulib.c`, formatting/dirent wrappers in `user/posix.c`, `setjmp`, and enough compatibility to experiment with a `dash` port.
- Userland bootstrap is now more Unix-like: `init` runs `dash /etc/rc.d/rc.S`, tracks runlevels, handles `telinit` requests via `SIGHUP`, and `exec` supports `#!` interpreter scripts.
- **NVMe driver** now has I/O queue creation and synchronous READ/WRITE command support via PRP1 (single-page transfers).
- **E1000 driver** (Intel Gigabit Ethernet) now has full ifnet integration with TX/RX descriptor rings, IRQ handling, and proper network interface registration.
- **PCNET driver** (AMD PCNET-PCI II) now has full ifnet integration with TX/RX rings, initialization block, and IRQ handling.
- **RTL8111 driver** (Realtek Gigabit Ethernet) promoted from probe stub to full implementation with descriptor-based TX/RX, IRQ handling, and ifnet integration.
- **VMXnet3 driver** stub added for VMware paravirtualized NIC support (PCI detection, BAR mapping, MAC address reading).
- **Hyper-V NetVSC driver** stub added for Microsoft Hyper-V synthetic network adapter (requires VMBus infrastructure for full implementation).

---

## Current Subsystem Status

### ✅ Mature Subsystems (70-90% complete)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| VFS Layer | 90% | Multi-backend, mount table, longest-prefix matching |
| ext2 filesystem | 85% | Read/write, directories, inode management, default rootfs build target |
| FAT/msdosfs | 80% | ~1650 LOC, FAT12/16/32, short/long filenames |
| Process model | 85% | fork/exec/wait, process groups, sessions |
| Job control | 80% | setpgid, setsid, tcsetpgrp, terminal control |
| Signal handling | 95% | Full userspace delivery, alarm(), SIGPIPE, hardware fault mapping |
| Bootstrapping / init | 75% | VFS-launched init, rc scripts, runlevels, telinit, shebang exec |
| Memory management | 80% | Virtual memory, page tables, kalloc/kfree |

### ⚠️ Partially Implemented (30-60%)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| TCP/IP stack | 80% | UDP works, DNS/resolver works, TCP handshake + data + retransmission + teardown work; flow control still basic |
| Networking interfaces | 60% | BSD ifnet abstraction, loopback, virtio-net, routing, DHCP tooling, outbound packet path |
| POSIX compatibility layer | 60% | Broad header coverage, libc-style shims, dash port experiments, many APIs still stubbed or partial |
| procfs | 60% | Basic process info, missing many nodes |

### 🚧 Early Or Stubbed
| Subsystem | Status | Notes |
|-----------|--------|-------|
| PCI subsystem | 80% | Bus 0 enumeration, BAR decode/mapping, helper APIs, `lspci`; MSI/MSI-X still missing |
| DMA support | 75% | Page-based DMA allocation with physical address tracking and alignment |
| Virtio storage | 70% | Working virtio core + virtio-blk, but still single-queue/minimal-feature oriented |
| Real NICs | 60% | E1000, PCNET, RTL8111 have full ifnet integration; VMXnet3, Hyper-V netvsc are stubs |
| Modern storage | 40% | AHCI has polling DMA read/write; NVMe has I/O queue and basic RW path |
| Symlinks | None | VFS supports but not implemented |

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
void irq_unregister(int irq);
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
- [ ] Implement discard/write-zeroes request helpers behind capability checks
- [ ] Add error accounting and robust retry policy for transient I/O failures
- [x] Add runtime flush cadence tuning (`/proc/vblk_flush`) for write-heavy workloads
- [ ] Add optional queue-depth tuning knobs (single queue retained as default)

**Definition of done:**
- [x] Multiple virtio disks can be attached and independently read/written/mounted
- [x] No hardcoded device-0 behavior remains in I/O and capacity paths
- [ ] Flush/discard/write-zeroes are feature-gated and return deterministic errors when unsupported
- [ ] Stress pass: repeated mount/fsck-like write cycles complete without data corruption

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
**Status:** E1000, PCNET, RTL8111 have full ifnet integration; VMXnet3 and netvsc are stubs  
**Files:** `kernel/driver/e1000.c`, `kernel/driver/pcnet.c`, `kernel/driver/rtl8111.c`, `kernel/driver/vmxnet3.c`, `kernel/driver/netvsc.c`  
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

## Priority Tier 5: Filesystem Enhancements (Weeks 19-22)

### 5.1 Symbolic Links [HIGH]
**Current:** VFS has type but not implemented  
**Tasks:**
- [ ] Add S_IFLNK handling
- [ ] symlink syscall
- [ ] readlink syscall
- [ ] Path resolution symlink following
- [ ] Symlink loop detection

**File:** `vfs.c`, `sysfile.c`  
**Estimate:** 4-5 days

### 5.2 ISO 9660 (CD-ROM) [MEDIUM]
**Status:** Stub at `kernel/fs/vfs_isofs.c`  
**Value:** Read ISO images, distribution media  
**Tasks:**
- [ ] Volume descriptor parsing
- [ ] Directory traversal
- [ ] File reading
- [ ] Rock Ridge extensions (optional)

**Estimate:** 1 week

### 5.3 devfs [LOW]
**Current:** Device nodes created manually  
**Target:** Dynamic /dev population  
**Estimate:** 1 week

---

## Priority Tier 6: POSIX Compliance (Weeks 23-28)

### 6.1 Missing Syscalls [HIGH]
| Syscall | Priority | Complexity | Notes |
|---------|----------|------------|-------|
| ~~lseek~~ | ~~Critical~~ | ~~Low~~ | ✅ Implemented 2026-03-30 |
| ~~dup2~~ | ~~Critical~~ | ~~Low~~ | ✅ Implemented 2026-03-30 |
| ~~fcntl~~ | ~~High~~ | ~~Medium~~ | ✅ Implemented 2026-03-30 |
| select/poll | High | Medium | I/O multiplexing |
| mmap | High | High | Memory mapping |
| ioctl | High | Medium | Device control |
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
- `sys/socket.h` - Socket interface
- `netinet/in.h` - Internet addresses
- `arpa/inet.h` - Address conversion
- Fuller `FILE`-style stdio / buffered stream support

### 6.3 Library Functions [LOW]
Substantial userspace support now exists in `user/ulib.c` and `user/posix.c`:
- String/memory routines (`memcpy`, `memcmp`, `strstr`, `strtok_r`, etc.)
- Basic stdlib coverage (`strtol`, `strtoul`, `qsort`, `bsearch`, `rand`, environment helpers)
- Formatting wrappers (`vsnprintf`, `snprintf`, `sprintf`, `vsprintf`) and POSIX `dirent` translation

**Still Needed:**
- Full stdio/`FILE *` model
- More complete socket-family and networking headers
- Additional portability wrappers for larger third-party ports

### 6.4 POSIX Porting And Init [ONGOING]
**Status:** Significant userland progress landed 2026-03-31  
**Files:** `user/posix.c`, `user/ulib.c`, `user/setjmp.S`, `user/init.c`, `user/runlevel.c`, `user/telinit.c`, `kernel/core/exec.c`  

**Delivered:**
- Enough libc/POSIX scaffolding to experiment with ported software such as `dash`
- `exec()` shebang support for interpreter scripts
- SysV-style init flow with `/etc/rc.d/rc.S`, runlevel transitions, and `telinit`/`runlevel` tooling
- More POSIX-like `kill(pid, sig)` behavior wired into signal delivery

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
| `kernel/driver/pcnet.c` | AMD PCNET-PCI II with full ifnet integration |
| `kernel/driver/rtl8111.c` | Realtek RTL8111/8168 Gigabit Ethernet with full ifnet integration |
| `kernel/driver/vmxnet3.c` | VMware VMXnet3 paravirtualized NIC stub |
| `kernel/driver/netvsc.c` | Microsoft Hyper-V NetVSC paravirtualized NIC stub |
| `kernel/driver/ahci.c` | AHCI/SATA driver with polling DMA read/write |
| `kernel/driver/nvme.c` | NVMe driver with I/O queue and basic RW path |

### Headers
| File | Description |
|------|-------------|
| `include/pci.h` | PCI definitions |
| `include/virtio.h` | Virtio definitions |
| `include/stddef.h` | Standard definitions |
| `include/stdint.h` | Integer types |
| `include/stdlib.h` | Standard library |
| `include/string.h` | String operations |
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
| `user/posix.c` | POSIX compatibility wrappers |
| `user/runlevel.c` | Current/previous runlevel reporting |
| `user/telinit.c` | Runlevel transition requests |

### Filesystem
| File | Description |
|------|-------------|
| `kernel/fs/vfs_isofs.c` | ISO 9660 filesystem scaffold |

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

1. **Expand procfs** - Add `/proc/uptime`, `/proc/meminfo` - 2 hours each
2. **Implement gettimeofday syscall** - 2 hours
3. **Back real getrlimit/setrlimit syscalls behind the existing header stubs** - 1 hour
4. **Add `sys/socket.h`, `netinet/in.h`, and `arpa/inet.h` compatibility headers** - 2-3 hours
5. **Polish virtio-net link state / diagnostics** - 2 hours

---

## Testing Infrastructure Needed

1. **Unit test framework** for kernel components
2. **QEMU scripting** for automated boot tests
3. **POSIX conformance test suite** (subset)
4. **Network test environment** with virtual bridge

---

## Next Steps (Recommended Order)

1. ~~**Immediately:** Harden TCP with retransmission, teardown, and better receive/window handling~~ **DONE 2026-03-31**
2. **Next:** Finish virtio-net polish and decide whether to prioritize a real hardware NIC driver or stay QEMU-first
3. **Then:** Finish virtio-blk rough edges (flush/discard, multi-device cleanup) and move on to AHCI
4. **After that:** Implement symbolic links and close VFS pathname semantics gaps
5. **In parallel:** Continue POSIX porting with missing socket headers, stdio gaps, and targeted syscall additions (`select/poll`, `ioctl`, `gettimeofday`)
6. **Later:** Expand procfs/devfs and keep reducing boot/userland rough edges in the SysV-style init path
