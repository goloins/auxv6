# auxv6 Development Changelog (2026-04)

This document holds the detailed 2026-04 change history that was moved out of `docs/ROADMAP.md` so the roadmap can stay focused on current status and forward planning.

## Recent Wins (2026-04 Snapshot)

- Kernel-core performance hardening landed: larger core limits, O(1) cache lookups for buffers/inodes, per-CPU allocator caching, faster `mycpu()`, scheduler idle `hlt`, and syscall-return signal fast paths.
- `top(1)` and `libterm` landed, backed by `/proc/loadavg` and per-process `cticks` accounting.
- Added `/proc/schedstat` with scheduler pass/idle-halt/pick counters to support Track 0 follow-through without high-risk scheduler redesign.
- `abrowse(1)` landed as a text-mode browser layered on the existing HTTP client path.
- Server7 gained a dedicated boot profile, `/proc/server7` ownership control, and session-aware startup policy scaffolding.
- Documentation/manpage coverage expanded substantially with markdown-aware `man(1)` and broader default userland docs.
- `gfxperf(1)` landed to compute `/proc/gfxstats` flush/render efficiency; counter math handles 32-bit wrap safely.
- Dirty-row bounded framebuffer-sync prototype was attempted then rolled back after login-path instability; runtime remains on known-good full-surface compare path.
- Audio Stage-1 tranche 1 landed: per-fd PCM stream state/ring buffering and stream lifecycle open/close hooks.
- Audio Stage-1 tranche 2 landed: poll/select readiness wiring for AUDIODEV stream descriptors.
- Audio Stage-1 tranche 3 landed: `/proc/audio_clients` observability and `audiostat` integration.
- Audio follow-on runtime fixes landed: `fcntl(F_SETFL/F_GETFL)` now toggles/reports stream `O_NONBLOCK`; `/proc/audio_clients` now includes owner pid.
- Audio Stage-2 tranche 1 landed: `audiod` minimal daemon scaffold (single-sink poll loop, silence writes, and xrun recovery) with build/manpage/docs integration.
- Audio close-path leak fix landed: stream slots are now released correctly on fd close/exit by matching the original file-object owner identity, fixing stale `/proc/audio_clients` entries after `audiod`/`audioctl` exit.
- Audio procfs summary fix landed: when the last stream closes, `/proc/audio` now resets global stream-state/queue counters instead of reporting stale values from prior runs.
- Audio Stage-2 tranche 2 landed: `audiod` gained one-shot mailbox control commands and `audiodctl` helper support for runtime status/reconfigure without daemon restart.
- Audio control UX follow-on: `audiodctl` now prints explicit queue feedback and warns when no live `audiod` process is detected at command time.
- Framebuffer bring-up and console performance advanced: DMA framebuffer allocation now reserves contiguous runs from freelist; tty upward scroll reuses existing framebuffer pixels instead of near-full rerender.
- Locking modernization landed: spinlock acquire-timeout + nested-acquire panic behavior, lock-failure owner diagnostics, and lockdep-lite rank checks/diagnostics under debug flags.
- Console locking split into domain-specific locks (`input_lock`, `tty_lock`, `gfx_lock`); login-path mismatch regression was fixed.
- Lockdep follow-ons fixed real lock-order bugs and rank-map gaps discovered under guest load (`lockprobe`, `ls`, sanctioned sleep handoff paths).
- Early lockdep bring-up ordering regression (`make qemu` vs `make qemu-nox`) was fixed by deferring enforcement until late boot.
- Validation policy for lock/console work was codified: both `make qemu` and `make qemu-nox`, plus full `lockprobe` matrix (including `-L`).
- Large-file hardening landed build-clean: 64-bit offsets/sizes in key file/inode/stat paths; lseek ABI follow-up (`sys_lseek64`/`_llseek`) landed.
- Mount metadata limits and mount capacity were widened and centralized with shared policy constants.
- Network fixed-limit cleanup landed: route/ARP capacities raised and centralized (`NET_ROUTE_TABLE_MAX`, `NET_ARP_CACHE_MAX`), with matching userspace query ceilings.
- Real NIC coverage expanded across both legacy/stub and second-wave families; descriptor-ring TX/RX follow-on landed for 10 second-wave drivers.
- Network observability follow-on landed: normalized link state and `/proc/net_dev` Link column surfaced in userland views.
- Modem and serial groundwork landed: probe-visible modem inventory (`/proc/modems`) and `/dev/ttyS0..3` runtime endpoints with per-minor isolation plus `/proc/serial_tty` visibility.
- Descriptor-ceiling modernization landed: dynamic per-process fdtable, split default/hard limits, unified enforcement, `/proc/fdlimits`, and `FD_CLOEXEC` end-to-end behavior.
- Early-boot growth guardrails landed (entry window fit assertions, kernel image size checks, per-build footprint reporting).
- Pipe and exec argument policy modernizations landed with compile-time fit/invariant guards.
- NVMe/loop dev-number collision was fixed (loop moved to dev 44-51), restoring stable `/dev/nda` behavior.
- AHCI controller-profile wiring now includes NVIDIA MCP79 SATA (10de:0ab5) as an explicit PCI-ID path in the shared AHCI driver, with MCP79-style capability masking for port multipliers (Linux `board_ahci_mcp79`-inspired).
- Storage diagnostics improved with `/proc/bdev_table` and `lsblk -v`.
- FAT32 NVMe workflow validated end-to-end; `fatregress -d /mnt/fat32` reported all checks passed.
- exFAT, btrfs, and ufs2 read-only first tranches are in-tree with documented scope boundaries.
- User stack growth (Area 5) is complete and guest validated (`stackgrowtest` 3/3 pass).
- TUN/TAP Phase 1 follow-on landed: nonpersistent tun close now unregisters interface state cleanly, `tuntest` is in-tree for nonblock/poll/ICMP self validation, and guest-visible `tun0` creation now lines up with `ifconfig`/`/proc/net_dev` semantics.
- TUN/TAP guest validation checkpoint passed end-to-end: `tuntest run-all` is green in guest and `tuntapctl destroy tun0` now removes interface state from both `ifconfig` and `/proc/net_dev`.
- TUN/TAP Phase 2 tranche started: TAP userspace write ingress now feeds `ether_input`, `tuntest` gained `tap-arp-self` plus `run-all-tap`, and first-pass L2 ARP self-validation is now in-tree.
- TAP Phase 2 guest checkpoint is now green: `tuntest tap-arp-self` and `tuntest run-all-tap` both pass on `tap0`, counters advance in `/proc/net_dev`, and `tuntapctl destroy tap0` removes interface state cleanly from `ifconfig` and `/proc/net_dev`.
- FireWire/IEEE 1394 OHCI scaffold progressed: boot-time PCI probe/attach, controller phase-state tracking (`init/ready/resetting/degraded`), IRQ + polled event paths, generation-guarded async context queue scaffold, timeout reaping, and expanded `/proc/firewire` queue-health telemetry.
- NVIDIA nForce MCP79 Ethernet (10de:0ab0) tranche landed: PCI attach, BAR0 MMIO map, descriptor-ring polling TX/RX datapath, INTx path with polling fallback, and truthful link-state updates (no forced-RUNNING attach state).
- nForce observability follow-on landed: new `/proc/nforce` node with per-interface mode/link/irq plus TX/RX/link transition counters to validate IRQ-vs-poll behavior quickly in guest.
- Core text/crypto utility tranche landed: `uniq`, `sort`, `sum`, `sleep`, `yes`, `true`, `false`, `sync`, `touch`, `md5sum`, `sha1sum`, `sha224sum`, `sha256sum`, `sha384sum`, `sha512sum`, `base32`, `base64`, plus minimal `asroot` with `/bin/sudo` compatibility symlink.
- Archive utility tranche landed: `tar` (ustar create/list/extract) and `gunzip`, backed by shared libc gzip helpers (`user/gzip.c`); `tar` supports gzip-compressed archive reads (`-z` and `.gz`/`.tgz` auto-detect) and create-mode gzip output (`tar -c -z`) via valid deflate stored blocks.
- Allocator incident documentation landed for `lsblk`-triggered kernel trap-14 during NVMe/Btrfs bring-up: see `docs/historic/kalloc-page-fault-investigation-2026-04-06.md` for symbolication, fault-path analysis, and debug plan.
- Post-incident stabilization/perf tranche landed: canonical kernel-PDE sync corrected to ignore volatile HW bits, first-fault VM attribution/logging expanded, kernel-stack robustness upgraded (8KB contiguous + reuse cache), and `kallocstress` slope-reduction optimizations shipped across pipe/file/fd/sys_write hot paths (chunked pipe I/O, transition wakeups, object caches, fd hinting, small-write stack fast path) with new per-syscall diagnostics for residual `pipe-page-churn` drift.
- Perf attribution follow-on landed: `/proc/schedstat` + `kallocstress` now report wakeup/wait scan diagnostics (plus wake-channel class counters), an unsafe tick-wakeup gating experiment was rolled back after runlevel regression, and a safe global tick-sleeper model (tracked in generic `sleep()`) now gates timer `wakeup(&ticks)` without excluding non-`sys_sleep` tick waiters.

## Past Changes (2026-03-30 to 2026-04-03)

- `ping` revised to run until SIGINT and print exit statistics.
- `traceroute` landed with ICMP ECHO probes and `IP_TTL` socket option support.
- `setsockopt`/`getsockopt` syscalls landed with `IPPROTO_IP` + `IP_TTL` support.
- AHCI recovery/retry instrumentation and interrupt-driven completion landed.
- Toolchain hardening landed (`-nostdinc`, libgcc helper checks for 32-bit target).
- libc portability tranches 1 and 2 landed (canonical headers, time/civil calendar, stdio seek/tell/scan, stream buffering).
- procfs breadth expanded (`/proc/ahci_tune`, `/proc/vblk_flush`, `/proc/meminfo`, `/proc/lsof`).
- devman boot integration landed with initial manpages and utility coverage.
- Virtio-blk stress/retry harnesses and multi-device probe fixes landed.
- Loop device hardening and `looptest` regression suite landed.
- Terminal/PTY compatibility tranches landed (query/reply behavior, job control, termcap upgrades, `termcheck` coverage).
- Framebuffer sync-path speedups and wrap-safe gfxperf delta accounting landed; later follow-ons improved scroll-heavy throughput.
- Intel graphics attach/probe stub (`kernel/driver/intel_gfx.c`) landed as early bring-up scaffolding.
- Audio Stage-0 follow-on probe tranche landed across common PCI audio families.

## Known Regressions and Constraints (Tracked)

- `NFILE` global table was fully antiquated by the P1-A tranche (replaced by per-process dynamic `fdtable`, `NOFILE_DEFAULT`/`NOFILE_HARD` policy). The former `NFILE=1024` trap-14 boundary is a moot historical note; the crash path no longer exists.
- TUN/TAP is partially landed (Phase 1 is guest validated; Phase 2 TAP ingress + ARP self-test are guest validated); broader TAP/L2 parity and full soak signoff remain pending.
- exFAT device-selection parity in `sys_mount` remains an open integration item.

## Cross-References

- Roadmap and active plan: `docs/ROADMAP.md`
- Kernel perf hardening map: `docs/kernel-perf-hardening.md`
- Allocator/VM blueprint: `docs/allocator-vm-refactor-blueprint.md`
- Audio stage docs: `docs/audio-stage1-tranche1-runtime.md`, `docs/audio-stage1-tranche2-readiness.md`, `docs/audio-stage1-tranche3-observability.md`
