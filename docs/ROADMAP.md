# auxv6 Modernization Roadmap

## Project Overview

auxv6 is an xv6-derived Unix-like operating system with significant enhancements including:
- Multi-backend VFS layer (xv6fs, ext2, msdosfs, procfs, tmpfs, isofs, btrfs, ufs2)
- BSD-style networking with TCP/IP stack
- Signal handling infrastructure
- Job control with process groups, sessions, terminal control
- Multi-filesystem support with mount table

**Architecture:** x86 32-bit, single address space per process  
**Current State:** ext2-root is the default boot path, the kernel has received a substantial 2026-04 performance-hardening pass, the first NFS read-only path is partially wired, server7 has an initial session-aware bootstrap, and the userland now includes a broader admin/TUI layer (`top`, `abrowse`, `man`, `which`, `lsof`, `file`) plus a materially stronger libc/POSIX portability baseline (`getrlimit`/`setrlimit`, `netdb`, `fnmatch`, `glob`, `scandir`, `nftw`, `fts`, C-locale scaffolding, and corrected `unlink`/`rmdir` semantics).

---

## Current Subsystem Status

### ✅ Mature Subsystems (75-95% complete)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| VFS Layer | 90% | Multi-backend, mount table, longest-prefix matching |
| ext2 filesystem | 90% | Read/write, directory create/link/unlink/rename, inode/block allocation, rollback hardening, and default rootfs build target |
| FAT/msdosfs | 84% | ~1650 LOC, FAT16/32 backend in-tree, short/long filenames, and validated FAT16 NVMe test-image workflow (`qemu-nvme-fat`) |
| TCP/IP stack | 82% | UDP works, DNS/resolver works, TCP handshake + data + retransmission + teardown work, and raw-socket tooling (`ping`, `traceroute`) is in place; flow control still basic |
| Process model | 85% | fork/exec/wait, process groups, sessions |
| Job control | 82% | setpgid, setsid, tcsetpgrp, terminal control, and tty-stop behavior integrated with the PTY stack |
| Signal handling | 95% | Full userspace delivery, alarm(), SIGPIPE, hardware fault mapping |
| Bootstrapping / init | 85% | VFS-launched init, rc scripts, runlevels, telinit, shebang exec, early-runlevel `devman` bootstrap, and dedicated server7 boot profile |
| Memory management | 83% | Virtual memory, page tables, kalloc/kfree, and a landed per-CPU allocator-cache hardening pass |
| PCI subsystem | 80% | Bus 0 enumeration, BAR decode/mapping, helper APIs, `lspci`; MSI/MSI-X still missing |
| DMA support | 78% | Page-based DMA allocation with physical address tracking, alignment support, and multi-page framebuffer-friendly backing |
| Loop devices | 88% | 8 block devices; setup validation hardened, busy-teardown guard, extended status (offset + mounted flag), `looptest` regression suite, 256 MB rootfs |
| ISO 9660 | 85% | Working read-only implementation with VFS integration and loop-mount testing |
| Symlinks | 90% | `symlink/readlink/lstat` wired, VFS follow/no-follow behavior landed, ext2 path traversal follows intermediate links, loop-depth limits and regression tests added |
| Terminal/PTY stack | 90% | Console + dynamic PTY allocation (`/dev/ptmx` -> `/dev/pts/N`), per-endpoint queue/termios/winsize/ioctl routing, job-control integration, query/reply compatibility work, and termcheck coverage |
| Virtio storage | 95% | Virtio core + virtio-blk with DoD checklist complete; queue-depth tuning and retry telemetry in place |

### ⚠️ Partially Implemented (50-74%)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| Networking interfaces | 68% | BSD ifnet abstraction, loopback, virtio-net, routing, DHCP tooling, outbound packet path, and multiple working drivers; link-state polish and broader real-hardware parity still lag |
| POSIX compatibility layer | 79% | Broader tty/ioctl compatibility, dynamic `openpty`/`ptsname_r` path, truthful time/stdio tranche work, `getrlimit`/`setrlimit`, minimal `netdb`, shell/text traversal helpers (`fnmatch`, `glob`, `scandir`, `nftw`, `fts`), C-locale scaffolding, and corrected `unlink`/`rmdir` semantics are in-tree; identity/account and stdio follow-on work remain |
| Userland docs/manpages | 82% | `man` now renders richer markdown and the tree ships 90+ documented utilities, but coverage depth and maintenance discipline still need work |
| Graphics / framebuffer console | 72% | Framebuffer core, display registry, builtin font/render path, rich `/proc/gfxstats`, virtio-gpu scanout discovery, display-sized framebuffer allocation, display-derived readable boot geometry, stable mirror behavior, ownership plumbing for server7, and recent sync-path speedups are landed; CGA still owns the canonical console path, virtio-gpu still uses whole-frame uploads, and no `/dev/fb0` or `/dev/dri/card0` ABI exists yet |
| procfs | 80% | `/proc/uptime`, `/proc/version`, `/proc/pci`, `/proc/vblk_flush`, `/proc/ahci_tune`, `/proc/meminfo`, `/proc/ps`, `/proc/loadavg`, `/proc/mountstats`, `/proc/gfxstats`, `/proc/lsof`, `/proc/server7`, `/proc/bdev_table`; breadth is now solid but per-process drill-down remains sparse |
| Real NICs | 60% | E1000, PCNET, and RTL8111 have full ifnet integration; virtio-net continues to improve; VMXnet3 has a basic polling datapath; netvsc, I219-V, I226-V, and AX88179 PCI remain stub-grade |
| Device node management | 68% | `devman -s` creates `/dev` nodes at early runlevel from kernel-visible inventory and `/etc/devman.conf` can tune verbosity, but policy rules, cleanup mode, and hotplug/event support are still pending |
| Modern storage | 91% | AHCI now has interrupt-driven completions, slot allocation, telemetry, and fault-injection hooks; NVMe correctness hardening complete (polled-only IRQ model, monotonic CID counter, recovery memory-safety, shutdown notification, LBA-size guard), dev-number collision with loop devices fixed, and ext2/ext2fs mount alias validated on `/dev/nda`; NVMe timeout/reset recovery path is in place |

### 🚧 Early Or Stubbed (0-49%)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| NFS | 45% | XDR/RPC transport, MOUNT plumbing, read-only VFS wiring, and basic GETATTR/LOOKUP/READ paths landed; READDIR decode stub still in place; deprioritized pending other work |
| Btrfs | 35% | Initial read-only VFS backend landed (`btrfs` mount type): single-device volumes, metadata-tree traversal, directory lookup/readdir, regular-file reads, and symlink reads; write paths, compression/RAID/multi-device, and many advanced features remain out of scope |
| UFS2/FFS | 28% | Initial read-only VFS backend landed (`ufs2`/`ffs` mount type): superblock probe, inode/directory traversal, direct/single-indirect file reads, and symlink reads with conservative format assumptions; write paths and broader on-disk compatibility hardening remain out of scope |
| Device hotplug/eventing | None | Planned kernel event path for live node add/remove beyond boot-time `devman -s` |

---

## Recent Wins (2026-04 Snapshot)

- Kernel-core performance hardening landed: larger core limits, O(1) cache lookups for buffers/inodes, per-CPU allocator caching, faster `mycpu()`, scheduler idle `hlt`, and syscall-return signal fast paths.
- `top(1)` and `libterm` landed, backed by new `/proc/loadavg` support and per-process `cticks` accounting.
- `abrowse(1)` landed as a text-mode browser layered on the existing HTTP client path.
- Server7 now has a dedicated boot profile, `/proc/server7` ownership control, and session-aware startup policy scaffolding.
- Documentation/manpage coverage expanded substantially, with markdown-aware `man(1)` support and a much broader default userland.
- NVMe/loop device-number collision was fixed by moving loop base to dev 44, restoring stable `nda` visibility in `lsblk` and successful NVMe ext2 mounting.
- Storage diagnostics improved with `/proc/bdev_table` and `lsblk -v`, making block-device registration/capacity state directly inspectable.
- `qemu-nvme-fat` image generation now uses dosfstools (`mkfs.fat`/`mkdosfs`) and produces a deterministic FAT16 image containing a known `README.TXT` marker for quick validation.
- Btrfs gained an initial read-only backend (`mount ... btrfs ...`) integrated through VFS, with explicit first-tranche constraints documented in `docs/btrfs-driver.md`.
- UFS2 gained an initial read-only backend (`mount ... ufs2 ...` and `mount ... ffs ...`) integrated through VFS, with first-tranche constraints documented in `docs/ufs2-driver.md`.
- Linux-host Btrfs image tooling landed (`tools/stage-btrfs-root.sh`, `make nvme-btrfs.img`, `qemu-nvme-btrfs`, `qemu-nox-nvme-btrfs`, `btrfs-reset`) to support repeatable guest validation.

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
  `confstr`/`pathconf`, and PTY POSIX wrappers. This tranche is now in-tree.
2. Tranche 2: time and stream correctness. Replace stub time surfaces and add
  stdio positioning, scanning, and basic truthful stream-buffering APIs that
  portable tools expect. This tranche is now basically closed and in-tree.
3. Unified libc/POSIX portability tranche: treat the remaining libc/runtime
  portability work before kernel-backed pthread support as one coordinated
  batch so headers, libc helpers, light syscall truthfulness, and porting
  surfaces can move together. The tranche now includes the shell/text and
  syscall-truthfulness slice (`fnmatch`, `glob`, `scandir`/`alphasort`, both
  tree-walk APIs, minimal `locale.h`, `getrlimit`/`setrlimit`, minimal IPv4
  `netdb.h`, corrected directory-removal semantics) plus the identity/stdio
  follow-on slice (`pwd.h`/`grp.h`, `getpwnam`/`getpwuid`/`getgrnam`/
  `getgrgid`, `fscanf`/`vfscanf`, and `tmpfile`).
4. Parallel thread-enablement track: replace placeholder `pthread_*` types with
  a real shared-address-space thread model, TLS-ready libc state, and a
  minimal pthread subset once the kernel/runtime pieces exist.

---

## Active Plan (2026-04)

Primary goal: consolidate recent kernel-core and userland wins into a dependable baseline while finishing the first usable NFS read-only milestone and teeing up the unified libc/POSIX portability tranche.

### Track 0 - Kernel-core performance follow-through
- Soak the recent allocator, scheduler, inode-cache, and buffer-cache changes under mixed `schedperf`/`fsperf`/`kallocstress` workloads.
- Add low-risk observability where it clarifies regressions quickly: cache/fallback counters, expanded procfs summaries, or other cheap diagnostics rather than another large scheduler rewrite.
- Keep higher-risk redesigns such as per-CPU run queues, MLFQ, or wider wakeup-structure changes out-of-scope until the current fast-path work is fully validated.

**Definition of done:**
- Repeated perf runs stay stable without reintroducing login/procfs regressions.
- At least one additional low-risk hot-path improvement is either landed or explicitly deferred with data.
- The current perf utilities remain good enough to compare changes across builds.

### Tranche A - Storage reliability hardening
- Virtio-blk: keep bounded retry policy but tighten transient-vs-fatal error accounting and operator-visible diagnostics.
- AHCI: finish mount/unmount endurance loops with timeout/recover telemetry and no controller lockups.
- NVMe: correctness hardening complete. Remaining gap: interrupt-driven completion path (currently polled).

**Current status (2026-04-03):**
- Virtio-blk retry telemetry and stress tooling are in-tree.
- AHCI recovery instrumentation, fault injection, and soak-oriented tuning hooks are in-tree.
- NVMe: spurious-IRQ bug fixed (polled-only mode), recovery memory leak and admin queue state reset fixed, monotonic CID counter added, LBA>BSIZE guard added, shutdown notification (`nvme_shutdown`) added and called from `sys_halt`.
- NVMe/loop dev-number collision fixed (`loop0..7` moved to dev 44-51); downstream userspace mapping (`mount`, `devman`) updated.
- `sys_mount` now accepts both `ext2` and `ext2fs` for ext2 backend selection.
- NVMe ext2 mount path validated with both numeric dev (`mount 40 ext2fs /mnt`) and name-based dev (`mount /dev/nda ext2fs /mnt`).
- FAT16 NVMe test image workflow validated under `sudo make qemu-nvme-fat`; generated image includes deterministic `README.TXT` content for no-mount hex-level verification.

**Definition of done:**
- `lsblk`/mount behavior remains stable across repeated attach/mount/unmount cycles on virtio-blk, AHCI, and NVMe.
- Unsupported storage operations fail predictably (no silent success, no panic).
- Timeout/error counters are visible through existing diagnostic paths.

### Tranche B - devman phase-2 policy improvements
- Add richer `/etc/devman.conf` rule parsing (path pattern -> mode/owner/group/action).
- Add optional stale-node cleanup mode for boot-time reconciliation.
- Keep hotplug/event mode out-of-scope for this tranche, but define kernel/userspace interface requirements.

**Current status (2026-04-03):**
- Boot-time static scan mode is in place.
- `/etc/devman.conf` already has a runtime debug knob, but not the richer policy engine yet.

**Definition of done:**
- Policy-driven node mode/ownership works in boot scan mode.
- Cleanup mode is opt-in and safe against active device nodes.
- Hotplug interface proposal is documented with concrete data structures and event semantics.

### Tranche C - observability and userland ergonomics
- Expand procfs coverage where low-risk and high-value (`/proc/net`, `/proc/sockets`, `/proc/lsof` filtering/polish, per-process filtering strategy notes).
- Continue manpage expansion and freshness checks for recently landed storage/network/admin/TUI tools.
- Add scripted smoke checks for `which`, `lsof`, `file`, and the newer procfs-backed userland where practical.

**Current status (2026-04-03):**
- `which`, `lsof`, `file`, `top`, and `abrowse` are in the default userland.
- `man(1)` has richer markdown support and the tree now carries broad baseline page coverage.
- `/proc/loadavg` and `/proc/server7` landed since the earlier roadmap snapshot.

**Definition of done:**
- New utilities are covered by scripted smoke tests in QEMU boot runs.
- Manpages exist for each tool promoted to default userland in this tranche.
- Procfs breadth improves without committing prematurely to a large per-process ABI.

**Scope note:**
- Framebuffer/screen-draw performance work (dirty-rect updates, partial flush,
  glyph batching, reduced full-frame mirror cost) is acknowledged as a major
  optimization area, but intentionally tracked as a separate graphics/server7
  stream from the current kernel-core performance hardening work.

### Tranche D - NFS read-only (deprioritized)
- NFS is intentionally on the backburner. The existing XDR/RPC/UDP transport and basic VFS wiring are in-tree and not being removed.
- READDIR stub decode remains incomplete; live-server validation has not been run.
- Return to this tranche when the base kernel/storage and userland work has settled further.

**Current status (2026-04-03):**
- XDR + RPC codec path landed in kernel.
- MOUNT portmapper and basic NFSv3 procedure RPCs (GETATTR, LOOKUP, READ, READDIR stub) are in place.
- VFS NFS backend wired as read-only filesystem type.
- READDIR decode intentionally deferred.
- Detailed notes: `docs/nfs-v3-integration.md`.

### Tranche E - server7 bootstrap follow-through
- Keep `make qemu-server7` and init integration as the dedicated graphics bootstrap path while preserving default boot behavior.
- Build on the landed display ownership control path and early protocol (`HELLO`, `STATUS`, `PING`) instead of reworking it.
- Decide the next lowest-risk boundary between tty/console and server7 ownership: input queue handoff, richer status metadata, or small client-side protocol growth.
- Preserve the startup policy split aligned with unix session semantics:
  - authenticated terminal user session -> draw desktop directly (main flow)
  - init/system launch -> present GUI login flow modeled after A/UX login dialog

**Current status (2026-04-03):**
- Boot profile and init startup path landed.
- `/proc/server7` read/write control landed.
- Console-side ownership arbitration hooks landed.
- Server7 protocol bootstrap and ownership claim/release wiring landed.
- Startup flow policy scaffold landed in server7 with auto-detection and explicit mode override.

**Definition of done:**
- `make qemu-server7` boots with server7 started by init and uses login-dialog flow.
- Running `server7` from an authenticated user tty session enters desktop-direct flow.
- Server7 can claim and release display ownership through kernel control path.
- Early clients can complete protocol handshake and retrieve flow/status metadata.

---

## Next Steps (Recommended Order)

1. **Continue Track 0 (kernel-core performance follow-through)** before taking on another broad structural kernel rewrite.
2. **Execute Tranche A (storage reliability hardening)** — NVMe correctness work is done; AHCI soak validation and virtio-blk diagnostics tightening remain.
3. **Execute Tranche B (devman policy parsing + optional cleanup)** to strengthen `/dev` lifecycle safety.
4. **Execute Tranche C (observability + manpages + utility smoke tests)** to lock in operational confidence around the larger default userland.
5. **Tranche D (NFS) is deprioritized** — return to it after the base kernel/storage work has settled; the immediate blocker remains READDIR decode.
6. **After storage/devman/observability:** broaden the new Btrfs backend (search/index efficiency, feature coverage, and validation), then return to the unified libc/POSIX portability tranche and devman hotplug/event lifecycle enhancements.

### Prepared Next Slice (2026-04-03)

The next prepared tranche after the newly landed libc identity/stdio work is:

- Thread groundwork:
  - replace placeholder `pthread_*` typedef expectations with a real thread plan

### Btrfs Tranche (Read-Only Follow-on)

Current position:
- Initial read-only support is in-tree and usable for single-device images.
- Linux-host test-image plumbing exists for NVMe attach validation.

Missing before the next milestone bump:
- Compressed-extent read support.
- Stronger malformed-metadata hardening and fail-closed validation.
- Key-directed lookup/search path improvements to reduce broad tree scans.
- Broader test coverage (mount/read/readdir/readlink negative and corruption cases).

Definition of done for next bump:
- Read-only mount/read paths remain stable across repeated NVMe boot/mount cycles.
- At least one non-trivial compressed image reads successfully.
- Lookup/read hot paths have measurable scan reduction versus the current baseline.
  - choose shared-address-space thread creation/join/TID semantics
  - make libc state ready for eventual TLS-backed thread correctness where needed
- Residual portability cleanup:
  - writable `fmemopen` semantics only if a real caller requires them
  - fuller `perror` parity
  - broader address-family/name-service work only when backing is real
- validation and truthfulness follow-through:
  - focused probes for passwd/group lookup and formatted input
  - doc/manpage refresh for the now-canonical `/etc/group` path and libc lookup behavior

---

## Remaining POSIX/Libc Gaps (Selected)

- Missing syscalls: `mmap`; broader `ioctl` coverage for non-tty devices.
- libc/POSIX portability tranche scope now remaining: writable `fmemopen` semantics if required by real callers, fuller `perror` parity, and broader address-family/name-service work beyond the current truthful IPv4 `netdb` subset.
- threads: real kernel/libc thread support remains to be designed and implemented; placeholder pthread typedefs are not runtime support.
- Headers: fuller socket-family declarations/constants and any broader resolver interfaces only when their backing is real.

---

## Recommended Low-Level Continuations

1. **Finish NVMe interrupt-driven completion path** — the polled model works but an IRQ handler would allow concurrent I/O without blocking the CPU; the `irq_register` infrastructure is already in place.
2. **Start the thread groundwork slice** so the current portability baseline can grow into real pthread support instead of placeholder types.
3. **Expand procfs with `/proc/net`, `/proc/sockets`, and filtered fd views** to make perf/network/storage debugging cheaper.
4. **Finish NVMe interrupt-driven completions** to match AHCI's IRQ-driven model and reduce poll overhead.
5. **Polish virtio-net link-state and diagnostics** now that poll/IRQ instrumentation exists.
6. **Tighten the remaining stdio/runtime truthfulness edges (`fmemopen` writable semantics only if needed, `perror` parity)** before treating the portability tranche as stable.

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

#### 3.2 NVMe + FAT16 Validation Path [COMPLETE]
**Status:** Validated 2026-04-03.  
**Files:** `kernel/driver/loop.c`, `kernel/core/sysfile.c`, `kernel/fs/procfs.c`, `kernel/core/blockdev.c`, `user/lsblk.c`, `user/mount.c`, `user/devman.c`, `Makefile`, `docs/nvme-driver.md`  
**Definition of done:**
- [x] Fixed dev-number overlap that let loop registration overwrite NVMe (`ND_DISK_BASE` at 40, loop moved to 44-51)
- [x] Added low-friction block-device diagnostics (`/proc/bdev_table`, `lsblk -v`)
- [x] Added `ext2fs` alias support in mount dispatch (alongside `ext2`)
- [x] Confirmed NVMe ext2 mounts via both numeric and `/dev/nda` forms
- [x] Confirmed `qemu-nvme-fat` produces mountable FAT16 media with deterministic `README.TXT` marker bytes

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
| `kernel/fs/vfs_ufs2.c` | UFS2/FFS read-only filesystem with initial VFS integration |
| `user/devman.c` | Device node manager utility with boot-time static scan mode (`devman -s`) |

---

## Past Changes (2026-04-03) — kernel performance hardening

Ten targeted optimisations addressing the worst xv6-heritage bottlenecks, all
without restructuring the core proc/lock model.  Every change compiled and
linked cleanly against the full kernel tree.

### Resource limits
- `NPROC` 64→128, `NFILE` 100→256, `NINODE` 50→200, `NOFILE` (per-process) 16→32, `NBUF` 30→128 (decoupled from `LOGSIZE`).  The 30-buffer cache was the most acute bottleneck; 15 KB of cache for an entire OS workload caused nearly-constant eviction and disk I/O.

### Allocator (`kernel/core/kalloc.c`)
- `kfree()` debug `memset(v, 1, PGSIZE)` is now gated behind `-DKDEBUG_KFREE_POISON`.  Production builds no longer write 4 KB of junk on every page free.  `exec()` frees the old address space page-by-page, making this a frequent hot path.

### Spinlock (`kernel/core/spinlock.c`)
- Added `pause` (x86 `PAUSE` hint) in the spin-wait loop.  On HT/SMT cores this reduces bus-lock traffic and avoids the memory-order violation that can stall a pipeline when the lock owner releases while the spinner holds a stale cacheline.
- `getcallerpcs()` (10-frame `%ebp` walk) is now conditional on `-DKDEBUG_SPINLOCK_CALLSTACK`.  This call was executed on *every* `acquire()` in the kernel.

### `mycpu()` (`kernel/core/proc.c`, `kernel/driver/mp.c`)
- `mpinit()` now builds a 256-entry `apic_cpu_map[]` reverse table: `apicid → cpus[] index`.  `mycpu()` replaced its O(ncpu) LAPIC-ID linear scan + per-call LAPIC MMIO read with a single array lookup.  `mycpu()` is called from `myproc()`, `acquire()`, `release()`, `sched()`, `yield()`, and every lock-related path, so this is a hot fix.

### Scheduler (`kernel/core/proc.c`)
- **Per-CPU scan start offset** (`cpu.sched_last`): each CPU remembers the index just past the last process it ran and starts its next scan there.  Eliminates the tendency for all CPUs to race for the same low-indexed slots and spreads scheduling naturally across the full table.
- **Idle `hlt`**: when a scheduler pass finds zero RUNNABLE processes, the CPU releases `ptable.lock` and executes `hlt`, suspending until the next interrupt.  Previously all CPUs spun in a tight loop repeatedly acquiring/releasing `ptable.lock` at the timer rate, even when the system was completely idle.  This was the dominant source of inter-CPU lock contention on a lightly-loaded system.

### Timer ISR (`kernel/core/proc.c`, `kernel/core/sysproc.c`)
- Added `active_alarm_count` (an atomic counter) that tracks the number of processes with live alarms.  `proc_check_alarms()` now returns immediately (no `ptable.lock` acquire, no O(NPROC) scan) when the counter is zero.  On a system with no `alarm()`-using processes this eliminates two `ptable.lock` acquire/release cycles per 10 ms tick on CPU 0.
- `proc_set_alarm()` helper maintains the counter; `sys_alarm` delegates through it.

### Syscall-return signal dispatch (`kernel/core/trap.c`, `kernel/core/proc.c`)
- The three back-to-back `ptable.lock` acquire/release/acquire/release/acquire/release sequences (`proc_apply_pending_signals` + `proc_deliver_signal` + `proc_maybe_stop_current`) at every syscall and trap return are now wrapped in a single `proc_handle_signals_on_return()` call with a lockless fast-path precheck.  When `sig_pending`, `sig_caught`, and `state` are all clear, no lock is touched at all.

### Buffer cache (`kernel/fs/bio.c`, `include/buf.h`)
- Added a 64-entry hash table (`bcache.hash[BCACHE_HASH_SIZE]`) alongside the existing LRU doubly-linked list.  `bget()` cache-hit lookups are now O(1) (average 2 comparisons) instead of O(NBUF) = O(128).  Eviction still uses the LRU list; on eviction the buffer is removed from its old hash chain and inserted into the new one.  A `B_INHASH` flag tracks whether a slot is presently in a chain.

### Inode cache (`kernel/fs/fs.c`, `include/file.h`)
- Added a 64-entry hash table (`icache.hash[ICACHE_HASH_SIZE]`) with the same design.  `iget()` cache-hit lookups are now O(1) instead of the previous O(NINODE) = O(200) linear scan under `icache.lock`.  Slot recycling removes the old entry from the hash and inserts the new assignment.

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
- Kernel-core performance hardening landed: larger core limits, buffer/inode hash tables, faster `mycpu()`, per-CPU allocator caching, scheduler idle `hlt`, and syscall-return signal fast paths.
- `top(1)` and `libterm` landed, with `/proc/loadavg` plus per-process `cticks` support behind them.
- `abrowse(1)` landed as the first in-tree text-mode browser built on the existing HTTP client path.
- `make e1000` and `make qemu-server7` boot profiles were added for more targeted bring-up and validation.
- virtio-net runtime diagnostics/polling resilience improved, alongside follow-on fixes in `telnet`, `sh`, and DHCP tooling.
- Framebuffer console sync-path speedups landed, reducing full-refresh overhead without changing the current ownership model.
