# auxv6 Modernization Roadmap

## Project Overview

auxv6 is an xv6-derived Unix-like operating system with significant enhancements including:
- Multi-backend VFS layer (ext2, msdosfs, procfs, tmpfs, isofs, btrfs, ufs2, exfat, nfs)
- BSD-style networking with TCP/IP stack
- Signal handling infrastructure
- Job control with process groups, sessions, terminal control
- Multi-filesystem support with mount table

**Architecture:** x86 32-bit, single address space per process  
**Current State:** ext2-root is the default boot path, the kernel has received a substantial 2026-04 performance-hardening pass, the first NFS read-only path is partially wired, server7 has an initial session-aware bootstrap, and the userland now includes a broader admin/TUI layer (`top`, `abrowse`, `man`, `which`, `lsof`, `file`) plus a materially stronger libc/POSIX portability baseline (`getrlimit`/`setrlimit`, `netdb`, `fnmatch`, `glob`, `scandir`, `nftw`, `fts`, C-locale scaffolding, and corrected `unlink`/`rmdir` semantics).

---

## Current Subsystem Status

### ✅ Mature Subsystems
| Subsystem | Status | Notes |
|-----------|--------|-------|
| VFS Layer | 92% | Multi-backend dispatch (`ext2/msdosfs/exfat/btrfs/ufs2/isofs/tmpfs/nfs`), mount table, longest-prefix matching |
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

### ⚠️ Active / Mixed-Maturity Subsystems
| Subsystem | Status | Notes |
|-----------|--------|-------|
| Networking interfaces | 75% | BSD ifnet abstraction, loopback, virtio-net, routing, DHCP tooling, outbound packet path, and broad real-NIC coverage are in-tree; normalized link-state visibility is wired through `/proc/net_dev`; tun Phase 1 is guest-validated end-to-end, and TAP Phase 2 now has guest-validated L2 ingress + ARP self-test coverage with clean destroy semantics; broader TAP parity and wider real-hardware soak still lag |
| POSIX compatibility layer | 83% | Broader tty/ioctl compatibility, dynamic `openpty`/`ptsname_r`, truthful time/stdio tranche work, `getrlimit`/`setrlimit`, minimal `netdb`, shell/text traversal helpers (`fnmatch`, `glob`, `scandir`, `nftw`, `fts`), C-locale scaffolding, identity/group lookup (`getpwnam`/`getpwuid`/`getgrnam`/`getgrgid`), and stdio follow-ons (`vfscanf`, `tmpfile`) are in-tree; remaining work is mostly thread/runtime truthfulness follow-through |
| Userland docs/manpages | 82% | `man` now renders richer markdown and the tree ships 90+ documented utilities, but coverage depth and maintenance discipline still need work |
| Graphics / framebuffer console | 74% | Framebuffer core, display registry, builtin font/render path, rich `/proc/gfxstats`, virtio-gpu scanout discovery, display-sized framebuffer allocation, display-derived readable boot geometry, stable mirror behavior, ownership plumbing for server7, dirty-rect tracking scaffolding, and sync-path speedups are landed; Intel display-class PCI attach scaffolding (probe + MMIO BAR map) is in-tree as a basic bring-up stub; CGA still owns the canonical console path, virtio-gpu still uses whole-frame uploads, and no `/dev/fb0` or `/dev/dri/card0` ABI exists yet |
| procfs | 87% | `/proc/uptime`, `/proc/version`, `/proc/pci`, `/proc/vblk_flush`, `/proc/ahci_tune`, `/proc/meminfo`, `/proc/ps`, `/proc/loadavg`, `/proc/schedstat`, `/proc/vmstat`, `/proc/mountstats`, `/proc/gfxstats`, `/proc/lsof`, `/proc/fdlimits`, `/proc/server7`, `/proc/bdev_table`, `/proc/net_tcp`, `/proc/net_udp`, `/proc/net_dev`, `/proc/audio`, `/proc/audio_stats`, `/proc/audio_clients`, `/proc/serial_tty`, `/proc/modems`; breadth is strong, though deeper per-process drill-down can still improve |
| Real NICs | 84% | E1000, PCNET, RTL8111, I219-V, I226-V, AX88179, RTL8125, RTL8139/8139C/RTL8110S, tg3, bnxt, atlantic, skge, and via_rhine provide full ifnet TX/RX datapaths; all 10 second-wave families (`igb`, `ixgbe`, `i40e`, `ice`, `bnx2`, `bnx2x`, `mlx4_en`, `mlx5e`, `ena`, `alx`) now have phase-2 descriptor-ring TX/RX: igb uses the e1000-class register map identical to I219, ixgbe uses the 82599/X540 register map with legacy descriptors, alx uses TPD/RFD/RRD rings, bnx2/bnx2x use BD rings with context-indirect init and status-block completions, i40e/ice wire QTX_TAIL/QRX_TAIL, mlx4/mlx5 implement WQE SQ/RQ+CQ rings with UAR doorbell, and ena implements SQ/CQ pairs with phase-bit completion. VMXnet3 keeps its basic polling datapath and netvsc remains VMBus-gated until transport support lands. |
| Device node management | 90% | `devman -s` creates `/dev` nodes from kernel inventory; `/etc/devman.conf` now carries full glob-pattern→mode policy rules; `devman -c` removes stale nodes; `devman -d` daemonizes with double-fork+setsid and periodic cleanup loop; hotplug event fd remains the only open item |
| Modern storage | 91% | AHCI now has interrupt-driven completions, slot allocation, telemetry, and fault-injection hooks; NVMe correctness hardening complete (polled-only IRQ model, monotonic CID counter, recovery memory-safety, shutdown notification, LBA-size guard), dev-number collision with loop devices fixed, and ext2/ext2fs mount alias validated on `/dev/nda`; NVMe timeout/reset recovery path is in place |

### 🚧 Early Or Stubbed
| Subsystem | Status | Notes |
|-----------|--------|-------|
| NFS | 45% | XDR/RPC transport, MOUNT plumbing, read-only VFS wiring, and basic GETATTR/LOOKUP/READ paths landed; READDIR decode stub still in place; deprioritized pending other work |
| exFAT | 35% | Initial read-only backend parser is in-tree (boot-sector validation, entry-set traversal, case-insensitive lookup, regular-file reads), but the mount device-selection path still needs parity wiring in `sys_mount`; write/allocate/truncate/rename metadata paths and robust seeded-image tooling remain out of scope |
| Btrfs | 35% | Initial read-only VFS backend landed (`btrfs` mount type): single-device volumes, metadata-tree traversal, directory lookup/readdir, regular-file reads, and symlink reads; write paths, compression/RAID/multi-device, and many advanced features remain out of scope |
| UFS2/FFS | 28% | Initial read-only VFS backend landed (`ufs2`/`ffs` mount type): superblock probe, inode/directory traversal, direct/single-indirect file reads, and symlink reads with conservative format assumptions; write paths and broader on-disk compatibility hardening remain out of scope |
| Audio subsystem | 45% | Stage-0 ABI/core is in-tree with ioctl + procfs surfaces and PCI-family probe stubs; Stage-1 tranches 1-3 are now landed with per-fd stream objects/ring buffers, blocking + `O_NONBLOCK` PCM write semantics, poll/select readiness wiring, and `/proc/audio_clients` observability via `audiostat`; Stage-2 tranches 1-2 are landed with a minimal `audiod` daemon scaffold plus local runtime control (`audiodctl`) for status/reconfigure; capture/hardware backends and broader policy compatibility remain follow-on |
| Device hotplug/eventing | None | Planned kernel event path for live node add/remove beyond boot-time `devman -s` |

---

## Recent Wins (2026-04 Snapshot)

Highlights are now summarized here and tracked in detail in `docs/CHANGELOG-2026-04.md`.

- Kernel-core perf and correctness hardening consolidated: allocator/cache improvements, lockdep-lite rollout, and stable COW slice progress.
- Storage and filesystem coverage broadened: NVMe/AHCI hardening, FAT32 validation, plus initial read-only btrfs/ufs2/exfat tracks.
- Networking and observability matured: broader NIC coverage, link-state visibility, and procfs instrumentation growth.
- Userland/admin ergonomics advanced: `top`, `abrowse`, `man`, `which`, `lsof`, `file`, and richer docs/man coverage.
- Audio Stage-1 tranches 1-3 are landed with stream lifecycle, readiness, and observability wired, and Stage-2 tranches 1-2 are landed with `audiod` runtime control scaffolding.

For full chronology and implementation-level detail, see `docs/CHANGELOG-2026-04.md`.

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

1. Tranche 1 (truthful wrappers/helpers): landed.
2. Tranche 2 (time + stdio correctness): landed.
3. Unified portability tranche (shell/text traversal, identity/group lookup, stdio follow-ons, truthful syscall surfaces): largely landed.
4. Thread-enablement track (real pthread-capable kernel/libc model): still open and is now the main remaining tranche.

Detailed tranche inventory and remaining items live in `docs/libc-posix-unified-tranche-inventory.md`.

---

## Active Plan (Summary)

Primary goal: hold the current kernel/userland gains as the stable baseline while finishing the next low-risk interoperability and reliability slices.

### In-progress priorities
1. Kernel-core COW/perf follow-through: keep the current Phase-4-slice-2 baseline stable (`kallocstress` 92/100, `schedperf` 87/100, `fsperf` 87/100), then harden correctness before wider COW expansion. Details: `docs/allocator-vm-refactor-blueprint.md` and `docs/kernel-perf-hardening.md`.
2. Storage reliability follow-through: keep AHCI/NVMe/virtio behavior stable under repeat attach/mount/unmount cycles; finish NVMe interrupt-driven completion. Details: `docs/nvme-driver.md`.
3. TUN/TAP completion path: Phase 0 is landed, Phase 1 is guest-validated, and the first Phase 2 TAP slice is guest-validated (`run-all-tap` + destroy). Complete remaining TAP/L2 parity plus soak/regression signoff. Details: `docs/tuntap-design-and-phase-plan.md`.
4. Libc/POSIX thread-enablement and remaining truthfulness edges: threads remain the major unlanded runtime feature; portability surface should only expand where kernel backing is real. Details: `docs/libc-posix-unified-tranche-inventory.md`.
5. Server7/graphics boundary follow-through: continue ownership/bootflow integration while keeping canonical console paths stable. Details: `docs/graphics-subsystem-design.md` and `docs/framebuffer-implementation-vt-summary.md`.

### Known limitations (current)
- Descriptor policy now uses per-process dynamic `fdtable` plus `NOFILE_DEFAULT`/`NOFILE_HARD`; the historical global `NFILE` boundary issue is obsolete. Details: `docs/death-of-xv6.md` and `docs/CHANGELOG-2026-04.md`.
- Missing syscall families remain, notably `mmap`, plus broader non-tty `ioctl` coverage.
- Modem stack remains early (probe visibility and baseline tty wiring exist; deeper UART, pgrp/session, and AT/PPP behavior remains).
- TUN/TAP is partial (tun control plane and Phase 1 path are guest-validated; TAP has a guest-validated starter slice with L2 ingress and ARP self-test, but broader L2 parity, compatibility edges, and soak signoff remain).

## Recommended Next Steps

1. Finish TUN/TAP through TAP/L2 parity and regression suite signoff (`docs/tuntap-design-and-phase-plan.md`).
2. Finish NVMe interrupt-driven completion path (`docs/nvme-driver.md`).
3. Land thread groundwork so libc portability can move beyond placeholder pthread types (`docs/libc-posix-unified-tranche-inventory.md`).
4. Wire exFAT device-selection parity in `sys_mount` (`docs/exfat-driver.md`).
5. Continue audio beyond Stage-1 tranche 3 with Stage-2 hardware backend bring-up and OSS-compat follow-on (`docs/audio-subsystem-implementation-plan.md`).

## Testing And Tooling Gaps

1. Kernel-focused deterministic unit-test harness for core subsystems.
2. Wider guest smoke/stress automation beyond current expect coverage.
3. POSIX conformance subset aligned with current libc portability surface.
4. Repeatable network integration fixture (virtual bridge + DHCP/DNS).

## Detailed References

- Rolling implementation log: `docs/CHANGELOG-2026-04.md`
- Kernel perf/COW architecture and validation: `docs/allocator-vm-refactor-blueprint.md`, `docs/kernel-perf-hardening.md`
- Storage backends: `docs/nvme-driver.md`, `docs/msdosfs.md`, `docs/exfat-driver.md`, `docs/btrfs-driver.md`, `docs/ufs2-driver.md`
- TUN/TAP design and execution phases: `docs/tuntap-design-and-phase-plan.md`
- NFS status and scope: `docs/nfs-v3-integration.md`
- Libc/POSIX tranche inventory: `docs/libc-posix-unified-tranche-inventory.md`
- Graphics/server7 and framebuffer notes: `docs/graphics-subsystem-design.md`, `docs/framebuffer-implementation-vt-summary.md`
- Audio staged roadmap: `docs/audio-subsystem-implementation-plan.md`, `docs/audio-stage0-contract-pack.md`, `docs/audio-stage1-tranche1-runtime.md`, `docs/audio-stage1-tranche2-readiness.md`, `docs/audio-stage1-tranche3-observability.md`, `docs/audio-stage2-tranche1-daemon-scaffold.md`, `docs/audio-stage2-tranche2-control-path.md`
- Descriptor-limit modernization and rationale: `docs/death-of-xv6.md`

---

## Change History

The detailed 2026-04 implementation log has been moved to `docs/CHANGELOG-2026-04.md` so this roadmap stays focused on current status, active work, and forward priorities.

Historical highlights covered there include:
- kernel-core perf and locking modernization details;
- storage/network/libc tranche landings and follow-on fixes;
- framebuffer and audio stage-by-stage updates;
- guest validation checkpoints and known regressions.
