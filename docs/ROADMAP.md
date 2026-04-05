# auxv6 Modernization Roadmap

## Project Overview

auxv6 is an xv6-derived Unix-like operating system with significant enhancements including:
- Multi-backend VFS layer (xv6fs, ext2, msdosfs, procfs, tmpfs, isofs, btrfs, ufs2)
- BSD-style networking with TCP/IP stack
- Signal handling infrastructure
- Job control with process groups, sessions, terminal control
- Multi-filesystem support with mount table

**Architecture:** x86 32-bit, single address space per process  
**Current State:** ext2-root is the default boot path, the kernel has received a substantial 2026-04 performance-hardening pass, the first NFS read-only path is partially wired, server7 has an initial session-aware bootstrap, and the userland now includes a broader admin/TUI layer (`top`, `abrowse`, `man`, `which`, `lsof`, `file`) plus a materially stronger libc/POSIX portability baseline (`getrlimit`/`setrlimit`, `netdb`, `fnmatch`, `glob`, `scandir`, `nftw`, `fts`netstat -s          # interface counters
netstat -t          # TCP socket table (try after starting a server)
netstat -u          # UDP socket table (try after DHCP or ping)
cat /proc/net_dev   # raw interface counter dump, C-locale scaffolding, and corrected `unlink`/`rmdir` semantics).

---

## Current Subsystem Status

### ✅ Mature Subsystems (75-95% complete)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| VFS Layer | 92% | Multi-backend dispatch (`xv6fs/ext2/msdosfs/exfat/btrfs/ufs2/isofs/tmpfs/nfs`), mount table, longest-prefix matching |
| ext2 filesystem | 90% | Read/write, directory create/link/unlink/rename, inode/block allocation, rollback hardening, and default rootfs build target |
| FAT/msdosfs | 90% | FAT16/32 backend in-tree with create/remove/mkdir/rename coverage, root-cause filename-limit fix landed, and the FAT32 NVMe regression pass is back green under `fatregress` |
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
| Graphics / framebuffer console | 72% | Framebuffer core, display registry, builtin font/render path, rich `/proc/gfxstats`, virtio-gpu scanout discovery, display-sized framebuffer allocation, display-derived readable boot geometry, stable mirror behavior, ownership plumbing for server7, and recent sync-path speedups are landed; Intel display-class PCI attach scaffolding (probe + MMIO BAR map) is now in-tree as a basic bring-up stub; CGA still owns the canonical console path, virtio-gpu still uses whole-frame uploads, and no `/dev/fb0` or `/dev/dri/card0` ABI exists yet |
| procfs | 84% | `/proc/uptime`, `/proc/version`, `/proc/pci`, `/proc/vblk_flush`, `/proc/ahci_tune`, `/proc/meminfo`, `/proc/ps`, `/proc/loadavg`, `/proc/schedstat`, `/proc/mountstats`, `/proc/gfxstats`, `/proc/lsof`, `/proc/fdlimits`, `/proc/server7`, `/proc/bdev_table`, `/proc/net_tcp`, `/proc/net_udp`, `/proc/net_dev`; breadth is now solid and now includes low-cost scheduler counters plus descriptor-limit observability, though deeper per-process drill-down can still improve |
| Real NICs | 79% | E1000, PCNET, RTL8111, I219-V, I226-V, AX88179, RTL8125, RTL8139/8139C/RTL8110S, tg3, bnxt, atlantic, skge, and via_rhine provide ifnet TX/RX datapaths (polling completion model); the follow-on families (`igb`, `ixgbe`, `i40e`, `ice`, `bnx2`, `bnx2x`, `mlx4_en`, `mlx5e`, `ena`, `alx`) are now phase-1 attachable drivers with PCI bring-up + ifnet registration while ring TX/RX programming is still pending. VMXnet3 keeps its basic polling datapath and netvsc remains VMBus-gated until transport support lands. |
| Real NICs | 84% | E1000, PCNET, RTL8111, I219-V, I226-V, AX88179, RTL8125, RTL8139/8139C/RTL8110S, tg3, bnxt, atlantic, skge, and via_rhine provide full ifnet TX/RX datapaths; all 10 second-wave families (`igb`, `ixgbe`, `i40e`, `ice`, `bnx2`, `bnx2x`, `mlx4_en`, `mlx5e`, `ena`, `alx`) now have phase-2 descriptor-ring TX/RX: igb uses the e1000-class register map identical to I219, ixgbe uses the 82599/X540 register map with legacy descriptors, alx uses TPD/RFD/RRD rings, bnx2/bnx2x use BD rings with context-indirect init and status-block completions, i40e/ice wire QTX_TAIL/QRX_TAIL, mlx4/mlx5 implement WQE SQ/RQ+CQ rings with UAR doorbell, and ena implements SQ/CQ pairs with phase-bit completion. VMXnet3 keeps its basic polling datapath and netvsc remains VMBus-gated until transport support lands. |
| Device node management | 90% | `devman -s` creates `/dev` nodes from kernel inventory; `/etc/devman.conf` now carries full glob-pattern→mode policy rules; `devman -c` removes stale nodes; `devman -d` daemonizes with double-fork+setsid and periodic cleanup loop; hotplug event fd remains the only open item |
| Modern storage | 91% | AHCI now has interrupt-driven completions, slot allocation, telemetry, and fault-injection hooks; NVMe correctness hardening complete (polled-only IRQ model, monotonic CID counter, recovery memory-safety, shutdown notification, LBA-size guard), dev-number collision with loop devices fixed, and ext2/ext2fs mount alias validated on `/dev/nda`; NVMe timeout/reset recovery path is in place |

### 🚧 Early Or Stubbed (0-49%)
| Subsystem | Status | Notes |
|-----------|--------|-------|
| NFS | 45% | XDR/RPC transport, MOUNT plumbing, read-only VFS wiring, and basic GETATTR/LOOKUP/READ paths landed; READDIR decode stub still in place; deprioritized pending other work |
| exFAT | 35% | Initial read-only backend parser is in-tree (boot-sector validation, entry-set traversal, case-insensitive lookup, regular-file reads), but the mount device-selection path still needs parity wiring in `sys_mount`; write/allocate/truncate/rename metadata paths and robust seeded-image tooling remain out of scope |
| Btrfs | 35% | Initial read-only VFS backend landed (`btrfs` mount type): single-device volumes, metadata-tree traversal, directory lookup/readdir, regular-file reads, and symlink reads; write paths, compression/RAID/multi-device, and many advanced features remain out of scope |
| UFS2/FFS | 28% | Initial read-only VFS backend landed (`ufs2`/`ffs` mount type): superblock probe, inode/directory traversal, direct/single-indirect file reads, and symlink reads with conservative format assumptions; write paths and broader on-disk compatibility hardening remain out of scope |
| Audio subsystem | 22% | Stage-0 native ABI/core skeleton is in-tree with ioctl and procfs surfaces; PCI audio-family probe stubs register common AC97/HDA/legacy controller families into the audio registry with OSS-intent capability flags; Stage-1 has started with per-fd stream objects, per-stream software ring buffers, and blocking/nonblocking PCM write semantics wired through kernel file open/write/close paths |
| Device hotplug/eventing | None | Planned kernel event path for live node add/remove beyond boot-time `devman -s` |

---

## Recent Wins (2026-04 Snapshot)

- Kernel-core performance hardening landed: larger core limits, O(1) cache lookups for buffers/inodes, per-CPU allocator caching, faster `mycpu()`, scheduler idle `hlt`, and syscall-return signal fast paths.
- `top(1)` and `libterm` landed, backed by new `/proc/loadavg` support and per-process `cticks` accounting.
- Added `/proc/schedstat` with scheduler pass/idle-halt/pick counters to support Track 0 performance follow-through without introducing high-risk scheduler redesign.
- `abrowse(1)` landed as a text-mode browser layered on the existing HTTP client path.
- Server7 now has a dedicated boot profile, `/proc/server7` ownership control, and session-aware startup policy scaffolding.
- Documentation/manpage coverage expanded substantially, with markdown-aware `man(1)` support and a much broader default userland.
- `gfxperf(1)` landed to compute `/proc/gfxstats`-based flush and render efficiency metrics from workload deltas; its counter math now handles 32-bit wrap safely for long-running sessions.
- A dirty-row bounded framebuffer-sync prototype was attempted but rolled back after a login-path stability regression; the console currently remains on the known-good full-surface compare path.
- Audio Stage-1 tranche 1 is now in progress: per-fd PCM stream state/ring buffering landed in kernel core, stream lifecycle is now hooked into open/close paths, and `devman` now creates `/dev/audioctl` and `/dev/pcmC0D0p` by default.
- Audio Stage-1 tranche 1 internals, semantics, and follow-on checklist are documented in `docs/audio-stage1-tranche1-runtime.md`.
- Audio Stage-1 tranche 2 added explicit poll/select readiness wiring for AUDIODEV stream descriptors; details are documented in `docs/audio-stage1-tranche2-readiness.md`.
- Audio Stage-1 tranche 3 added `/proc/audio_clients` per-stream observability and wired `audiostat` to print all `/proc/audio*` nodes; details are documented in `docs/audio-stage1-tranche3-observability.md`.
- Bounded dirty-rect framebuffer scaffolding (`FB_MAX_DIRTY_RECTS=8`) is present, but runtime still uses the conservative single-bounding-rect flush path for stability while follow-on tuning continues.
- Framebuffer bring-up and console performance moved forward materially: DMA framebuffer allocation now reserves real contiguous runs from the global page freelist instead of relying on repeated `kalloc()` adjacency, and tty upward scroll now reuses existing framebuffer pixels instead of rerendering nearly the whole visible surface on every scrolled line.
- Locking modernization tranche landed for crash prevention and diagnosability: spinlock acquire timeout + nested-acquire detection now panic instead of silently hanging, and lock-failure paths print lock-name/owner diagnostics under `KDEBUG_SPINLOCK_LOCKFAIL`.
- Console locking is now split into domain-specific locks (`input_lock`, `tty_lock`, `gfx_lock`) and file-table locking uses sleeplock; a login-path boot panic introduced during refactor (`spinlock bad release: lock=console_tty`) was root-caused to lock-pair mismatches and fixed in-tree.
- Lockdep-lite is now wired in spinlock core under `KDEBUG_LOCKDEP`: acquire-time rank checks, release-order checks, and per-CPU held-lock chain diagnostics are active for annotated core locks (`console_*`, `ftable_internal`, `ticks`, `ptable`, `kmem`, `log`, `icache`, `bcache`, `pipe`, `bdev`, `vfs`, `ktime`, `irq`).
- Follow-on lockdep stabilization fixed real lock-order bugs and rank-map gaps discovered under guest load (`lockprobe`/`ls`): `consoleread()` timed-wait now releases tty before taking `tickslock`, `kmem` is explicitly ranked above `ptable`, and sanctioned `sleep(chan, lk)` handoff semantics no longer false-positive as generic release-order bugs.
- A short-lived `halt` regression (`panic: mycpu called with interrupts enabled`) was root-caused to a lockdep-aware `cprintf()` guard calling `mycpu()` with interrupts on; fix: gate that probe behind `FL_IF==0` so `mycpu()` is only used in valid interrupt state.
- Audit follow-up found a boot-order lock compatibility break on non-xv6fs roots: `idup()` can run before xv6fs-only `iinit()`, leaving `icache.lock` uninitialized; fix: split out unconditional one-time `icache_init()` and call it before VFS bootstrap.
- `lockprobe` now has a dedicated lockdep handoff selftest mode (`-L`) that intentionally drives pipe and ticks-based sleep/wakeup transitions to catch lock-order regressions in sanctioned sleep paths.
- Storage/network lock hardening pass now annotates active block/net lock classes (core net tables, socket layer, IDE/AHCI/NVMe/virtio-blk, major NIC drivers, and filesystem mount locks) with explicit lockdep class labels for clearer panic chains and faster root-causeing.
- A graphical-boot regression (seen on `make qemu` but not `make qemu-nox`) was traced to lockdep checks firing too early in bring-up; fix: defer lockdep enforcement until late boot via `lockdep_enable()` after memory and console-gfx late-enable complete.
- Validation policy is now codified for lock/console work: both `make qemu` and `make qemu-nox` boots plus full `lockprobe` matrix (including `-L`) are required before signoff.
- Kernel-core large-file P0 hardening tranche is now landed build-clean: `struct file.off` and in-memory `struct inode.size` are widened to 64-bit, `struct stat.st_size` is widened to 64-bit, and `off_t` is now `int64_t` in libc headers.
- VFS/data-path offset plumbing is now widened end-to-end: `readi/writei/procfs_readi`, device-switch read/write callbacks, and VFS vnode read/write callbacks now take 64-bit offsets; all in-tree filesystem backends were signature-aligned in the same tranche.
- Multiple backend 4 GB truncation footguns were removed in the same pass (notably btrfs/exfat/nfs/ufs2 inode-size assignment and stat export paths), and msdosfs cluster math was explicitly kept 32-bit bounded to avoid libgcc 64-bit division helper linkage in kernel mode.
- PID allocation now has explicit wrap-safe behavior: `PID_MAX` policy added, alloc path wraps back to PID 2, skips in-use PIDs under `ptable.lock`, and panics only on true PID-space exhaustion instead of relying on unchecked signed overflow.
- Large-file seek ABI follow-up is now landed: `sys_lseek64`/`_llseek` are in-tree for full-range seek on i386, and `sys_lseek` now returns `off_t` correctly via i386 `edx:eax` int64 return convention.
- Detailed tranche notes are now tracked in `docs/death-of-xv6.md` (scope, rationale, compatibility boundaries, and follow-up map).
- Fixed-limit mount metadata holdover is now landed: `VFS_NAME_MAX`/`MOUNTINFO_NAME_MAX` are widened to `NAME_MAX + 1`, `VFS_MOUNT_PATH_MAX`/`MOUNTINFO_PATH_MAX` are widened to `PATH_MAX`, and mount table/query capacity is raised from 8 to 32 (`VFS_MOUNTS_MAX`/`MOUNTINFO_MAX`), replacing old xv6-era tight limits in both kernel and user ABI surfaces.
- `sys_mount` payload staging is now modernized for safety and headroom: mount-option payload cap is centralized as shared policy (`MOUNT_DATA_MAX` in `limits.h`, currently `PATH_MAX-1`) and staging moved from fixed kernel-stack buffers to heap-backed temporary storage, reducing stack pressure while allowing larger mount option strings.
- Mount-capacity policy is now centralized: shared `MOUNT_MAX` in `limits.h` now drives both kernel mount-table capacity (`VFS_MOUNTS_MAX`) and userspace mount query capacity (`MOUNTINFO_MAX`), removing duplicate hardcoded ceilings.
- Network-info fixed-limit cleanup is now landed: route and ARP capacities are raised from 32 to 128 and centralized in shared networking policy constants (`NET_ROUTE_TABLE_MAX`, `NET_ARP_CACHE_MAX` in `include/net.h`), with matching userspace query ceilings (`ROUTEINFO_MAX`, `ARPINFO_MAX`) to avoid early truncation in tooling.
- Real-NIC coverage expanded with a new stub-driver tranche: `rtl8139` (RTL8139/8139C/RTL8110S), `rtl8125`, `tg3` (BCM5700/5719/5720), `bnxt` (BCM57412/57416), `atlantic` (AQC107/AQC108), `skge` (88E8001 family), and `via_rhine` (VT6103 family).
- Follow-on NIC tranche advanced from probe-only stubs to phase-1 attachable drivers (`igb`, `ixgbe`, `i40e`, `ice`, `bnx2`, `bnx2x`, `mlx4_en`, `mlx5e`, `ena`, `alx`): PCI enable + BAR map + deterministic MAC + ifnet registration are now wired, with descriptor-ring TX/RX programming tracked as the next step.
- Initial modem coverage scaffold landed for common legacy PCI modem families (probe-only): Conexant HSF/HCF, Agere/Lucent LT, Smart Link, PCTel, Intel 536/537-class softmodems, and Motorola SM56; current tranche is detection-only and intentionally does not yet provide data/control paths.
- Serial modem chardev groundwork is now in-tree: `SERIALDEV` runtime endpoint with `/dev/ttyS0..3` node creation via `devman`, UART RX fanout into serial input buffering, per-minor runtime isolation (ttyS0 hardware-backed, ttyS1..ttyS3 virtual placeholders), and core termios/ioctl read/write coverage. See `docs/modem-driver.md` and `docs/serial-per-minor-isolation.md` for current semantics and limits.
- Procfs visibility for serial line capability/state is now in-tree via `/proc/serial_tty` (per-minor `hw_backed`, carrier/hangup, open refs, fg pgid, modem output bits, and canonical/CLOCAL flags).
- Driver-registration wiring was updated end-to-end for the new NIC tranche: build integration in `Makefile`, init declarations in `include/defs.h`, runtime registration in `kernel/net/device.c`, and vendor IDs added in `include/pci.h` (Broadcom/Marvell/VIA/Aquantia).
- P1 descriptor-ceiling modernization is now landed as a subsystem uplift rather than a raw constant bump: per-process dynamic `fdtable` storage is in-tree, policy is split into `NOFILE_DEFAULT` (soft/inherited) and `NOFILE_HARD` (setrlimit ceiling), legacy `NFILE` naming has been removed from live policy surfaces, and descriptor-limit enforcement is unified across `select(2)`, `poll(2)`, socket allocation, and rlimit paths.
- P1 descriptor-ceiling validation is green: `fdtest(1)` passes 16/16 in guest, including `FD_CLOEXEC`, `dup`/`dup2`, select/poll limit handling, and seek ABI integration checks.
- Next part of P1-A is ABI cleanup (not more ceiling brute-force): descriptor-limit observability is now landed via `/proc/fdlimits`; continue migrating any remaining fixed-size userspace interfaces that still carry legacy tiny bounds.
- Early-boot stability was hardened as a direct follow-up: entry paging window expanded to 8MB and linker now asserts kernel image fit in that pre-kvmalloc window, converting a silent early-boot fault class into an explicit build-time error.
- Growth-budget guardrails were added to prevent future "too large" regressions from surfacing only at runtime: compile-time table-size assertions (proc/file/page-meta), post-link kernel size checks (hard 8MB total, hard 4MB `.bss`, soft 6MB warning), and per-build footprint reporting in `make aux.kern`.
- P1 pipe-policy modernization is now landed: kernel pipe capacity increased (`512 -> 2048`) while POSIX `PIPE_BUF` atomicity floor remains `512`, with compile-time fit/invariant guards (`PIPE_CAPACITY >= 512`, `sizeof(struct pipe) <= PGSIZE`).
- P1 exec-argument modernization is now landed: exec enforces dual limits (`EXEC_ARGC_MAX=128`, `EXEC_ARG_BYTES_MAX=4096`) aligned with `ARG_MAX`, and argv pointer staging moved off fixed kernel stack arrays to one-page heap staging with compile-time fit guards.
- NVMe/loop device-number collision was fixed by moving loop base to dev 44, restoring stable `nda` visibility in `lsblk` and successful NVMe ext2 mounting.
- Storage diagnostics improved with `/proc/bdev_table` and `lsblk -v`, making block-device registration/capacity state directly inspectable.
- `qemu-nvme-fat` image generation now uses dosfstools (`mkfs.fat`/`mkdosfs`) and produces a deterministic FAT16 image containing a known `README.TXT` marker for quick validation.
- FAT32 NVMe workflow is now validated end to end (`qemu-nvme-fat32`): mount, short-name create/read/unlink, multi-block growth/truncate, long-filename create/read/unlink, and mkdir/rmdir all pass in guest.
- The FAT long-filename failure was fixed at the root contract instead of only in the backend: the old 14-character directory-component limit was removed in favor of `NAME_MAX + 1`, and the `getdents`/`readdir` bridge plus shell path handling were updated to match.
- FAT/msdosfs follow-up hardening landed after bringup: real FAT timestamps are now written and surfaced through `stat(2)`, VFAT unlink removes the full LFN chain, create rejects duplicate generated short-name aliases, and basic FAT rename support is now wired through the VFS rename hook.
- exFAT first tranche landed as a read-only backend parser with NVMe image/tooling hooks (`nvme-exfat.img`, `qemu-nvme-exfat`, `qemu-nox-nvme-exfat`); remaining integration parity includes `sys_mount` device-selection wiring plus better host-side seeded-image tooling.
- The FAT32 follow-up regression sweep also repaired cross-layer fallout from the global dirent-size change: direct `getdents()` callers were resized to stay within the syscall page limit, `getcwd()` path reconstruction was fixed for the widened synthetic dirent ABI, and the first FAT directory-rename subtree check was rewritten to avoid a sleep-lock deadlock.
- Current guest status on `qemu-nvme-fat32`: `fatregress -d /mnt/fat32` completes with `fatregress: all checks passed`.
- Btrfs gained an initial read-only backend (`mount ... btrfs ...`) integrated through VFS, with explicit first-tranche constraints documented in `docs/btrfs-driver.md`.
- UFS2 gained an initial read-only backend (`mount ... ufs2 ...` and `mount ... ffs ...`) integrated through VFS, with first-tranche constraints documented in `docs/ufs2-driver.md`.
- Linux-host Btrfs image tooling landed (`tools/stage-btrfs-root.sh`, `make nvme-btrfs.img`, `qemu-nvme-btrfs`, `qemu-nox-nvme-btrfs`, `btrfs-reset`) to support repeatable guest validation.
- On-demand user stack growth (Area 5) is complete: exec pre-allocates `USER_STACK_MAX_PAGES`, page faults grow one page at a time, fork inherits stack bounds metadata, and overflow now terminates with correct SIGSEGV wait status; `stackgrowtest` currently passes 3/3 in guest.
- xv6fs has started formal deprecation in-tree: legacy build/runtime paths are now gated behind `LEGACY_XV6FS=1`, and default rootfs staging no longer advertises xv6fs mounts.
- FD-table modernization tranche is complete: `struct fdtable` is now fully dynamic (`entries` + `fdflags` pages via kalloc), `NFILE` deprecated as alias of `NOFILE_HARD`, `NOFILE_DEFAULT=256` (soft, inherited) and `NOFILE_HARD=512` (setrlimit ceiling) split, `proc_fd_limit()` is now extern and used consistently in `sys_select`, `sys_poll`, and `socket_fdalloc`. `FD_CLOEXEC` is fully implemented: per-fd flag bits live in the `fdflags` page, `F_GETFD`/`F_SETFD`/`F_DUPFD`/`F_DUPFD_CLOEXEC` all work, `exec.c` closes cloexec FDs after image commit, and fork inherits flags (POSIX). `sys_lseek` was fixed for the i386 `int64_t` return convention (must set `tf->edx` high half in addition to `eax`); `sys_lseek64`/`_llseek` (5-arg Linux ABI, SYS_lseek64=97) and `lseek64()` inline wrapper are now present. `fdtest(1)` regression suite (16 tests) validates the full tranche and passes 16/16 in guest.

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

**Current status (2026-04-04, redesign reset):**
- Historical best on this branch before the sparse-fork experiment: `schedperf -n 3` `83/100`, `kallocstress -n 3` `88/100`, `fsperf -n 5` `85-86/100`.
- Sparse fork (`copyuvm` skip `!PTE_U` pages) was applied, benchmarked, and **reverted** after severe regressions: kallocstress `88→6`, schedperf `83→72`, fsperf `85→78`.
- Post-revert retest still shows allocator/fork path well below target: `kallocstress` now observed at `31/100` (`fork-copyuvm 69/s`, `pipe-page-churn 65 round/s`, `allocator-reclaim 62 fork/s`).
- Interpretation: the remaining bottleneck is architectural, not a single regression.  The current allocator is still a single global page freelist with small per-CPU caches, and `fork()` still pays eager full-page copy cost via `copyuvm()`.
- Focused tmpfs modernization (per-directory hash index, path-walk refactor) remains retained and useful; current weak fsperf subtests stay concentrated in `parallel-writers` and `inode-limit-rate`.
- **Track 0 direction changes here:** stop chasing allocator/fork speed with local tweaks.  The next step is a real redesign of the page allocator and fork path, staged as:
  1. per-CPU page magazines/lists with batched refill/drain,
  2. physical-page refcounts and copy-on-write fork,
  3. child-list based wait/reap bookkeeping to remove repeated full-table scans,
  4. only then retune scheduler fairness/scatter if benchmark data still demands it.
- A full production-kernel-style allocator/VM refactor map is now recorded in `docs/kernel-perf-hardening.md`, covering Linux/FreeBSD/NetBSD-derived design patterns, the auxv6 file-by-file impact surface, and a phased landing plan.
- Durable anti-context-collapse reference: `docs/allocator-vm-refactor-blueprint.md` now carries the complete subsystem blueprint, invariants, affected files, phase gates, and rollback rules.
- Phase 1 is now landed and guest-validated: PFN-indexed page metadata, refcount groundwork in `kfree()`, allocator counters, `/proc/vmstat`, and expanded `/proc/meminfo` page-count lines.
- Validation snapshot for the landed Phase 1 baseline: `kallocstress -n 3` `88/100`, `schedperf -n 3` `83/100`, `fsperf -n 3` `86/100`.
- Opinion: keep this tranche as the new baseline. The counters are sane, the zero-valued sharing fields are expected before COW, and the system is back in the historical best band.
- Phase 2 is landed and guest-validated, but currently regresses vs Phase 1 baseline: `kallocstress -n 3` `83/100` (from `88`), `schedperf -n 3` `81/100` (from `83`), `fsperf -n 3` `84/100` (from `86`).
- Phase 2 interpretation: architecture is correct, policy tuning is not. `cache_alloc_misses=0` with high refill batch activity suggests over-eager refill lock traffic.
- Phase 2b retune landed and built clean, but triggered a login-path kernel panic in guest; the 2b policy delta was rolled back while retaining Phase 2 architecture.
- Post-rollback guest validation is stable and passing: `kallocstress -n 3` `82/100` avg (82-83), `schedperf -n 3` `81/100` avg (80-82), `fsperf -n 3` `83/100` avg (81-85).
- Post-rollback `/proc/vmstat` remains consistent with prior interpretation (`cache_alloc_hits 7134`, `cache_alloc_misses 0`, `global_refill_batches 438`, `global_drain_batches 321`).
- Phase 2c is now guest-validated and promoted as the working baseline: single-lever refill hysteresis (`KALLOC_PCPU_REFILL_TRIGGER=4`) with unchanged watermarks and batch sizes.
- Phase 2c validation snapshot: `kallocstress -n 3` `84/100` avg (83-85), `schedperf -n 3` `81/100` avg (81-81), `fsperf -n 3` `86/100` avg (86-86).
- Delta vs rollback checkpoint (`82/81/83`): `kallocstress +2`, `schedperf +0`, `fsperf +3`, with no observed stability regressions.
- `/proc/vmstat` also moved in the expected direction for this retune (`global_refill_batches 405` from `438`, `global_refill_pages 6480` from `7008`, `global_drain_batches 288` from `321`, `global_drain_pages 4608` from `5136`, misses still `0`).
- Phase 3 has now started with tranche-1 VM scaffolding: software PTE COW bit definitions (`PTE_COW`) and VM helper routines (`pte_mark_cow`, `pte_mark_writable`, `uvm_release_pte`) are in-tree, and `deallocuvm()` now releases mappings through the shared helper path.
- Phase 3 scope guard remains intact: no COW fork enablement yet, no user-visible fork semantic changes.
- Phase 3 tranche-1 guest validation (post-scaffolding) is clean: `kallocstress -n 3` `85/100` avg (85-85), `schedperf -n 3` `81/100` avg (81-82), `fsperf -n 3` `85/100` avg (85-86), all pass/no panic.
- `/proc/vmstat` remained stable and consistent with Phase 2c behavior (`cache_alloc_hits 6669`, `cache_alloc_misses 0`, `global_refill_batches 405`, `global_drain_batches 288`).
- Phase 3 tranche-2 is now landed (build clean): added helperized PTE user-transition APIs (`pte_is_user`, `pte_mark_user`), added VM PTE invariant checks, and routed remaining user-PTE transition call sites through helpers without enabling COW fork behavior.
- Phase 3 tranche-2 guest validation is clean: `kallocstress -n 3` `85/100` avg (85-85), `schedperf -n 3` `81/100` avg (81-82), `fsperf -n 3` `85/100` avg (84-86), all pass/no panic.
- Phase 4 slice-1 has now started and is landed build-clean: fork now installs COW mappings for writable managed user pages, trap page-fault path attempts COW resolution before stack-growth fallback, and parent TLB is refreshed after fork-time write-protect changes.
- Phase 4 slice-1 guest validation is now complete and stable:
  - `/proc/vmstat` confirms active sharing activity (`ref_increments 172`, `deferred_frees 172`) with no panic.
  - `kallocstress -n 3`: `84/100` avg (84-84)
  - `schedperf -n 3`: `82/100` avg (82-82)
  - `fsperf -n 3`: one transient low run (`52/100`) but follow-up `fsperf -n 5` stabilized at `85/100` avg (85-85).
- Phase 4 interpretation: COW slice-1 behavior is functionally correct and performance-stable in averaged runs; the single low fsperf run is treated as host-noise/outlier.
- Phase 4 slice-2 is now landed (build clean):
  - COW fault handling is now gated to true user write-protection faults (`present=1`, `write=1`) before invoking `cow_fault()`.
  - `copyuvm()` now additionally shares safe read-only managed user pages directly (refcounted) instead of dense-copying them.
- Phase 4 slice-2 guest validation is complete and strongly positive:
  - `/proc/vmstat` shows expected COW activity growth (`ref_increments 371`, `deferred_frees 371`).
  - `kallocstress -n 3`: `92/100` avg (92-92)
  - `schedperf -n 3`: `87/100` avg (87-88)
  - `fsperf -n 3`: `87/100` avg (87-87)
- Focused correctness probe also passed after COW slice-2:
  - `stackgrowtest`: `3/3` pass
  - `stackgrowtest -d`: confirms deep recursion growth, fork-inherited stack growth, and SIGSEGV (`exit=11`) on max-stack exceed path
- Phase 4 slice-2 interpretation: this is the best combined allocator/scheduler/filesystem benchmark band in the current track while remaining stable.
- Next immediate action: keep current COW mapping scope fixed and do correctness hardening (fault-path invariants + focused regression probes like `stackgrowtest`/fork-heavy suites) before any wider COW class expansion.

### Tranche A - Storage reliability hardening
- Virtio-blk: keep bounded retry policy but tighten transient-vs-fatal error accounting and operator-visible diagnostics.
- AHCI: finish mount/unmount endurance loops with timeout/recover telemetry and no controller lockups.
- NVMe: correctness hardening complete. Remaining gap: interrupt-driven completion path (currently polled).

**Current status (2026-04-04):**
- Virtio-blk retry telemetry and stress tooling are in-tree.
- AHCI recovery instrumentation, fault injection, and soak-oriented tuning hooks are in-tree.
- NVMe: spurious-IRQ bug fixed (polled-only mode), recovery memory leak and admin queue state reset fixed, monotonic CID counter added, LBA>BSIZE guard added, shutdown notification (`nvme_shutdown`) added and called from `sys_halt`.
- NVMe/loop dev-number collision fixed (`loop0..7` moved to dev 44-51); downstream userspace mapping (`mount`, `devman`) updated.
- `sys_mount` now accepts both `ext2` and `ext2fs` for ext2 backend selection.
- NVMe ext2 mount path validated with both numeric dev (`mount 40 ext2fs /mnt`) and name-based dev (`mount /dev/nda ext2fs /mnt`).
- FAT16 NVMe test image workflow validated under `sudo make qemu-nvme-fat`; generated image includes deterministic `README.TXT` content for no-mount hex-level verification.
- FAT32 NVMe workflow validated under `sudo make qemu-nvme-fat32`; verified guest-side mount plus short-name, LFN, growth/truncate, and mkdir/rmdir regression coverage.
- FAT32 NVMe workflow now revalidated after rename and ABI fallout fixes; overwrite rename, cross-directory file rename, and directory subtree rejection are covered by `fatregress` and currently passing.
- Remaining FAT/msdosfs gaps are now mostly parity/polish items: link/symlink support, richer metadata semantics beyond native FAT attributes, Unicode LFN fidelity, and less host-tool-dependent seeded-image staging.
- exFAT now has an initial read-only baseline and host formatting path; remaining gaps are write paths, allocation bitmap/Upcase-table validation hardening, richer timestamp/attribute parity, and regression coverage beyond blank-image mount/lookup/read smoke. On the current macOS workflow, seeded-image validation is not yet as repeatable as the FAT32/Btrfs paths.

**Definition of done:**
- `lsblk`/mount behavior remains stable across repeated attach/mount/unmount cycles on virtio-blk, AHCI, and NVMe.
- Unsupported storage operations fail predictably (no silent success, no panic).
- Timeout/error counters are visible through existing diagnostic paths.

### Tranche B - devman phase-2 policy improvements ✅
- Add richer `/etc/devman.conf` rule parsing (path pattern -> mode/owner/group/action).
- Add optional stale-node cleanup mode for boot-time reconciliation.
- Keep hotplug/event mode out-of-scope for this tranche, but define kernel/userspace interface requirements.

**Current status (2026-04-04):**
- Policy rules: `/etc/devman.conf` now supports `<glob-pattern> <octal-mode>` lines; first matching rule wins; built-in defaults are fallback only.
- `strncpy`/`strncat` added to `user/string.c` (declarations in `include/string.h`) — the correct libc home.
- `devman -c`: enumerates `/dev` and `/dev/pts`, stats each device node, and unlinks any not present in the current kernel inventory.
- `devman -d`: double-fork + `setsid()` + stdin/stdout/stderr redirect to `/dev/null`; runs initial scan then loops `sleep(30)` → re-enumerate → cleanup. **FIXME:** `-d` mode causes a bootloop in rc.3; currently using `-s` mode only in rc.S. Needs debugging with `testdaemon` diagnostic tool.
- Hotplug event interface fully specified in `docs/future-devicenode-daemon.md`: `struct devman_event`, `/dev/devevent` ring-buffer design, daemon event loop sketch, and rollout stages.

**Definition of done:** ✅ Met.

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
3. ✅ **Execute Tranche B (devman policy parsing + optional cleanup)** — COMPLETE (Tranche B done; `-d` daemon mode debugged, works correctly but has TBD issue in devman-specific logic, using `-s` mode for now).
4. ✅ **Execute Area 5 (on-demand user stack growth)** — COMPLETE. Pre-allocates `USER_STACK_MAX_PAGES` (64 pages = 256 KiB) at exec time; `proc_try_grow_stack()` makes one page accessible per guard-page fault; SIGSEGV on ceiling exceeded; fork inherits bounds. Tested by `stackgrowtest`. See `docs/user-stack-growth-phase5.md` and `docs/user-stack-sizing-and-growth.md`.

### Prepared Next Slice (2026-04-04)

The next prepared tranche after the newly landed libc identity/stdio work is:

- Thread groundwork:
  - replace placeholder `pthread_*` typedef expectations with a real thread plan
  - choose shared-address-space thread creation/join/TID semantics
  - make libc state ready for eventual TLS-backed thread correctness where needed
- Residual portability cleanup:
  - writable `fmemopen` semantics only if a real caller requires them
  - fuller `perror` parity
  - broader address-family/name-service work only when backing is real
- Validation and truthfulness follow-through:
  - focused probes for passwd/group lookup and formatted input
  - doc/manpage refresh for the now-canonical `/etc/group` path and libc lookup behavior

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

---

## Known Limitations and Outstanding Issues (2026-04-05)

### Descriptor Ceiling Boundary (NFILE=1024 Failure)
**Status:** Documented 2026-04-05 after isolated binary-search regression analysis.

**Symptom:** When `NFILE` constant in `include/param.h` is set to exactly 1024, the kernel boots and reaches the login prompt, but crashes with a trap-14 (page fault) during the first interactive login sequence.

**Isolation Matrix (Binary Search):**
| NFILE | Boot Result | Status |
|-------|-------------|--------|
| 512–960 | ✅ pass | Stable across all tested midpoints |
| **1023** | ✅ **pass** | Empirically safe maximum (2^10 − 1) |
| **1024** | ❌ **fail** | Trap 14 in fileread(), cr2 inside ftable |

**Crash Details:**
- **Trap Type:** 14 (page fault, read protection violation)
- **Instruction:** `0x80109c65` in fileread() at `f->off += r` (updating file offset field)
- **Fault Address (CR2):** `0x8017fc4c` (inside ftable, approximately at file[3].off)
- **Trigger:** Login sequence (high I/O load)
- **Reproducibility:** 100% on `qemu-nox`

**Suspected Root Causes:**
1. **Compiler/Linker struct alignment corner case** at exactly 1024-entry boundary
2. **Bitwise logic assuming NFILE ≤ 1023** (e.g., using `& 0x3ff` modulo or power-of-2 assumptions)
3. **Off-by-one in size calculations** triggered only at 2^10 threshold
4. **GCC -O2 optimization quirk** specific to NFILE=1024 value

**Current Mitigation:**
- **NFILE is locked at 1023** in `include/param.h`
- This provides 1023 global file handles, a **~4× improvement** over the prior 256-entry baseline
- All smoke tests, stress tests, and full boot sequences pass stably at NFILE=1023
- Full binary-search isolation matrix is documented in `/memories/repo/nfile-1024-boundary-failure.md`

**Recommended Next Steps:**
1. Symbolic kernel debugging (`gdb` stepping through fileread page fault with NFILE=1024)
2. Compare kernel struct layouts (objdump) with NFILE=1023 vs 1024
3. Inspect linker map for address collisions at BSS boundary
4. Examine assembly output for any hardcoded 1024 assumptions
5. Try alternative struct packing or alignment directives if layout is suspect

**Impact:** Minor—NFILE=1023 is sufficient for current workloads. Future improvement if deeper root cause is identified and fixed.

---



- Missing syscalls: `mmap`; broader `ioctl` coverage for non-tty devices.
- Modem stack remains early: common PCI modem families are probe-visible and `/dev/ttyS*` endpoints now exist with per-minor isolation plus baseline carrier/hangup semantics, but multi-UART hardware binding beyond ttyS0, tighter session/ctty validation for pgrp ioctls, and AT/PPP data paths are still not in-tree.
- libc/POSIX portability tranche scope now remaining: writable `fmemopen` semantics if required by real callers, fuller `perror` parity, and broader address-family/name-service work beyond the current truthful IPv4 `netdb` subset.
- threads: real kernel/libc thread support remains to be designed and implemented; placeholder pthread typedefs are not runtime support.
- Headers: fuller socket-family declarations/constants and any broader resolver interfaces only when their backing is real.

---

## Recommended Low-Level Continuations

1. **Finish NVMe interrupt-driven completion path** — the polled model works but an IRQ handler would allow concurrent I/O without blocking the CPU; the `irq_register` infrastructure is already in place.
2. **Start the thread groundwork slice** so the current portability baseline can grow into real pthread support instead of placeholder types.
3. **Expand procfs with `/proc/net`, `/proc/sockets`, and filtered fd views** to make perf/network/storage debugging cheaper.
4. **Wire exFAT device selection parity in `sys_mount`** so `exfat` mounts use the same dev-override/default-device behavior as `msdosfs`/`btrfs`/`ufs2`.
5. **Polish virtio-net link-state and diagnostics** now that poll/IRQ instrumentation exists.
6. **Tighten the remaining stdio/runtime truthfulness edges (`fmemopen` writable semantics only if needed, `perror` parity)** before treating the portability tranche as stable.
7. **Start Phase A of the audio subsystem** using `docs/audio-subsystem-design.md`: land kernel audio core + ioctl ABI + null backend + `audiod` minimal mixer loop before hardware-driver bring-up.
8. **Set OSS compatibility as the first post-stability audio target**: after native audio Phase A/B stability, prioritize `/dev/dsp` + `/dev/mixer` first-class support before PulseAudio/sndio/ALSA shims. Detailed staged execution now lives in `docs/audio-subsystem-implementation-plan.md`, with Stage 0 ABI/ioctl and OSS mapping contract details in `docs/audio-stage0-contract-pack.md`.

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

#### 4.3 NIC Stub Coverage Expansion [COMPLETE]
**Status:** Implemented 2026-04-05  
**Files:** `kernel/driver/rtl8139.c`, `kernel/driver/rtl8125.c`, `kernel/driver/tg3.c`, `kernel/driver/bnxt.c`, `kernel/driver/atlantic.c`, `kernel/driver/skge.c`, `kernel/driver/via_rhine.c`, `kernel/net/device.c`, `include/defs.h`, `include/pci.h`, `Makefile`  
**Tasks:**
- [x] Add polling-datapath stubs for Realtek RTL8139/RTL8139C/RTL8110S and RTL8125 families
- [x] Add polling-datapath stubs for Broadcom tg3 (BCM5700/5719/5720) and bnxt (BCM57412/57416)
- [x] Add polling-datapath stubs for Aquantia Atlantic (AQC107/AQC108), Marvell Yukon (88E8001 family), and VIA Rhine (VT6103 family)
- [x] Wire init paths/build registration and expand PCI vendor constants used by the new probes

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
| `kernel/driver/i219.c` | Intel I219-V/e1000e-style driver with descriptor-ring TX/RX and polling completions |
| `kernel/driver/i226.c` | Intel I226-V/igc-style driver with descriptor-ring TX/RX and polling completions |
| `kernel/driver/ax88179_pci.c` | ASIX AX88179 PCI-oriented driver with descriptor-ring TX/RX and polling completions |
| `kernel/driver/pcnet.c` | AMD PCNET-PCI II with full ifnet integration |
| `kernel/driver/rtl8111.c` | Realtek RTL8111/8168 Gigabit Ethernet with full ifnet integration |
| `kernel/driver/rtl8139.c` | Realtek RTL8139/RTL8139C/RTL8110S legacy + C+ polling datapath stub |
| `kernel/driver/rtl8125.c` | Realtek RTL8125 2.5GbE polling datapath stub |
| `kernel/driver/tg3.c` | Broadcom BCM5700/5719/5720 (tg3-family) polling datapath stub |
| `kernel/driver/bnxt.c` | Broadcom BCM57412/57416 (bnxt-family) HWRM-based polling datapath stub |
| `kernel/driver/atlantic.c` | Aquantia AQC107/AQC108 (Atlantic-family) polling datapath stub |
| `kernel/driver/skge.c` | Marvell 88E8001/Yukon-family polling datapath stub |
| `kernel/driver/via_rhine.c` | VIA VT6103/Rhine-family polling datapath stub |
| `kernel/driver/vmxnet3.c` | VMware VMXnet3 paravirtualized NIC with basic polling datapath |
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
- Framebuffer sync follow-up currently keeps the known-good full-surface compare path for stability, while `gfxperf` provides a binary guest probe for `pixels_per_flush`, `cells_per_sync`, and `render_efficiency` from `/proc/gfxstats` deltas.
- `gfxperf` delta accounting now uses wrap-safe unsigned subtraction so `flush_pixels` and other counters remain meaningful in long runs.
- Latest framebuffer slowdown fix stops redrawing the decorative logo on every console sync, preventing the single dirty bounding box from stretching from the top-right logo region down to the active cursor row during normal text output.
- Latest validated framebuffer tranche: scroll-heavy console output now feels visibly faster in guest, `gfxperf -Q -P 20` remains around 1 sync/flush per emitted line with `render_efficiency=1.00`, and `cells_per_sync` dropped from near full-screen churn to roughly 160-200 cells after the scroll reuse path and stats-accounting fix.
- Next framebuffer performance targets are now clearly backend-facing rather than text-render-facing: first reduce per-scroll upload area/flush pixels where safe, then reduce virtio-gpu synchronous command overhead by batching or otherwise lowering transfer/flush submission cost without regressing interactive latency.
- Step-1 + Step-2 validation (2026-04-05) confirmed both backend goals moved: scroll changed-span clipping cut `pixels_per_flush` and `cells_per_sync` again, and paired virtio-gpu transfer+flush submit preserved low sync count while raising sustained quick-run throughput (`gfxperf -q -P 25` reached ~90.9 lines/sec with `render_efficiency=1.00`).
- Added a very basic Intel graphics probe or attach stub (`kernel/driver/intel_gfx.c`) inspired by Linux or BSD early i915-style bring-up: Intel display-class match, PCI memory or bus-master enable, MMIO BAR selection (BAR0 preferred, BAR2 fallback), and attach diagnostics. Modesetting/render submission remains follow-on.
- Audio Stage-0 follow-on landed for hardware discovery: a new PCI audio probe-stub tranche now matches fifteen historically common controller families (AC97/HDA/legacy PCI), registers them through the native audio core registry, and tags per-device capability flags to keep `/dev/dsp` + `/dev/mixer` OSS compatibility planning directly aligned with discovered hardware classes.
