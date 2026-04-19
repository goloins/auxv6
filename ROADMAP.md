# auxv6 ROADMAP (2026-04-19)

## Why This Revision Exists

The previous roadmap revision already moved away from historical planning, but
it is now behind the code again. The last 24-48 hours materially changed the
state of USB, PCI interrupt handling, and MLFQ scheduling. This revision resets
the roadmap to match the system that is actually in tree today.

## Evidence And Trust Model

This roadmap is based on:

1. Live code in `kernel/`, `libc/`, `user/`, `include/`, `ports/`, and build files.
2. Recent commits, with emphasis on 2026-04-18 through 2026-04-19.
3. Runtime and procfs interfaces exposed by the current tree.

This pass intentionally does not use project docs as roadmap authority.
Several source-file prologue comments are also stale and understate what is now
implemented, especially in PCI and virtio code. The roadmap below follows live
functions and call paths, not outdated banners.

## Changed Since 2026-04-18

1. USB moved from "host-controller scaffold exists" to "runtime orchestration is real".
- `usb.c` now maintains a runtime service loop with periodic refresh, IRQ-driven
  boost handling, deferred rescan/reenumeration/error-recovery queues, device
  materialization, attach/rebind/retire tracking, and `/proc/usb` telemetry.
- UHCI/OHCI/EHCI/xHCI all have reset/halt/start/scan/service/event-consumption
  backends.
- xHCI now has real control/bulk hooks for descriptor reads, configuration, and
  bulk submit/reap.

2. PCI interrupt support is no longer "future work" in practice.
- The PCI core now allocates and frees MSI/MSI-X vectors, programs MSI/MSI-X
  capability structures, and falls back to INTx when needed.
- Virtio-net, virtio-blk, and virtio-gpu all use the newer IRQ vector path.
- A virtio-net regression introduced during MSI/MSI-X work was fixed, which is
  exactly the kind of instability the roadmap should acknowledge.

3. MLFQ is now operationally tunable, observable, and regression-tested.
- `/proc/mlfq_tune` exposes runtime boost interval control.
- `/proc/schedstat` exports boost interval and budget-expiration counters in
  addition to queue/dispatch movement stats.
- `mlfqtune` is a first-class userland control utility.
- `schedperf` now carries an MLFQ-specific profile and tests aligned to the
  scheduler's real behavior, not legacy expectations.

## System Snapshot (Honest Assessment)

Legend:
- `Solid`: implemented and actively exercised
- `Mixed`: implemented and useful, but still carrying important limitations or risk
- `Scaffold`: attach/probe paths or partial plumbing exist, but the end-to-end path is incomplete

### 1) Kernel Internals

Status: `Mixed`, with scheduler work now clearly `Solid`

What is solid:
- The MLFQ scheduler is no longer just a fresh implementation; it is now an
  operational subsystem with 5 queues, demotion/promotion/boost behavior,
  runtime boost tuning, procfs observability, and regression/perf coverage.
- The scheduler hot path and `waitpid(pid)` path received targeted performance
  cleanup without changing semantics.
- Procfs observability continues to expand and is now part of how scheduler
  behavior is validated.

What is mixed/risky:
- VM/COW remains a high-complexity area with recent redesign history and should
  still be treated as a correctness risk surface.
- Locking and multi-CPU behavior remain an ongoing source of subtle failure.
- Core kernel maturity is uneven: scheduler confidence is rising faster than VM
  and some signal/job-control corners.

### 2) Filesystems And Storage

Status: `Mixed`

What is solid:
- ext2 and msdosfs remain core, practical filesystems.
- AHCI, NVMe, and virtio-blk are active storage paths, and the PCI interrupt
  work now benefits several storage-capable drivers as part of the common IRQ
  compatibility layer.
- Loop-device and ISO workflows are present and test-backed.

What is mixed/scaffold:
- exFAT, Btrfs, and UFS2 still look staged rather than boring.
- NFS remains partial.
- Data-safety expectations are still backend-dependent and should remain explicit.

### 3) Networking Stack And Drivers

Status: `Mixed`

What is solid:
- The in-kernel Ethernet/IP/ARP/ICMP/UDP/TCP stack exists and remains central.
- AF_UNIX and related socket work are real.
- Virtio-net is not merely present: it now rides the new PCI IRQ vector path,
  supports multi-vector-aware setup, and survived a recent MSI/MSI-X regression fix.
- Several classic PCI NIC drivers were updated to live in the newer MSI/MSI-X-aware world.

What is mixed/scaffold:
- Driver depth is still uneven; breadth remains ahead of confidence.
- The new USB RTL815x path is promising but still experimental: it attaches,
  binds endpoints, registers an ifnet, and services bulk traffic, but its
  control-plane programming and transmit path are not yet production-grade.
- Virtual/cloud-facing and less common NIC families still vary widely in maturity.

### 4) Audio

Status: `Mixed`

What is solid:
- Audio core ABI, stream lifecycle, and userland tooling remain real.
- AC97 continues to be the primary serious hardware path.

What is mixed/scaffold:
- Production policy, capture, and broader hardware-family depth still lag.
- PCI interrupt compatibility touched audio-related code, but audio is not yet a
  "boring" subsystem.

### 5) Graphics, Display, And UI

Status: `Mixed`, still a strategic focus

What is solid:
- Framebuffer/display/render/font layers remain present.
- `x6`/X11/`6wm` work is active, and UI bring-up is clearly a live development track.
- Virtio-gpu participates in the newer IRQ vector infrastructure and remains one
  of the more serious display paths in the tree.

What is mixed/scaffold:
- Graphics code still carries real TODOs in queue completion, response waiting,
  and broader device/display behavior.
- Intel graphics support is still not close to first-class.
- `6wm` should still be treated as active bring-up, not a settled desktop stack.

### 6) USB, Modem, Wi-Fi, WPAN, FireWire

Status: `Mixed/Scaffold`, but materially ahead of where it was yesterday

What is solid:
- USB host-controller discovery is no longer the whole story.
- UHCI/OHCI/EHCI/xHCI all have concrete controller bring-up hooks:
  probe/reset/halt/start/scan/service/event-consume.
- The USB core now has address management, device candidate tracking, runtime
  refresh, deferred recovery, and `/proc/usb` observability.
- xHCI supports descriptor fetch, config-descriptor fetch, set-configuration,
  and bulk submit/reap hooks.

What is mixed/scaffold:
- `usb.c` still describes itself as scaffold code, and that is still directionally true.
- Non-xHCI controllers do not yet have equivalent control/bulk datapaths.
- Attached-device coverage is narrow: RTL815x is the main serious path, and even
  that path is still experimental rather than trustworthy.
- Wi-Fi, WPAN, modem, and FireWire work remain mostly bring-up territory.

### 7) libc / POSIX Surface

Status: `Mixed`, improving through pressure from real software

What is solid:
- The libc and syscall surface is substantially broader than baseline xv6-style expectations.
- Porting pressure remains a good forcing function for correctness.

What is mixed:
- Threading is still the major missing capability.
- Some APIs remain compatibility-oriented stubs rather than complete semantics.

### 8) Userland Utilities, Admin Tools, And Tests

Status: `Solid`

What is solid:
- The userland inventory is broad and continues to grow.
- Stress and validation binaries are central to development, not afterthoughts.
- `mlfqtune` is exactly the kind of utility the project needs more of: small,
  direct, tied to a real kernel control surface.
- `schedperf` is now a better reflection of actual scheduler behavior and is part
  of the kernel correctness story.

What is mixed:
- Utility maturity still varies widely.
- High-level workflows are only as stable as the underlying kernel subsystems.

### 9) Ports

Status: `Mixed`, still strategically valuable

What is solid:
- Ports continue to expose real kernel/libc gaps.
- The ports effort is still one of the best ways to force honest Unix behavior.

What is mixed:
- Threading/runtime limitations still cap what some ports can realistically do.
- Recipe and staging policy are still evolving.

## Where The System Really Stands Right Now

The most important status change is this:

1. MLFQ is now a real operational feature, not an experimental branch of scheduler work.
2. PCI MSI/MSI-X support is partially productized in the core and actively used,
   but driver adoption is uneven and regressions are still plausible.
3. USB is no longer just discovery and documentation; there is now real runtime
   orchestration and an experimental xHCI-centered device path.
4. USB is still not broad or boring enough to call `Solid` as a subsystem.

## 30 / 60 / 90 Day Plan

## Next 30 Days (Stability First)

1. Freeze the MLFQ baseline.
- Keep `schedperf` green under repeated runs.
- Treat `/proc/mlfq_tune`, `/proc/schedstat`, and `mlfqtune` as supported interfaces.
- Add regressions around boost retuning, wakeup promotion, and budget-expiration behavior.

2. Harden the PCI interrupt matrix.
- Keep MSI/MSI-X/INTx fallback behavior explicit per driver.
- Add focused regression coverage around virtio-net and any driver touched by the new IRQ code.
- Stop relying on stale source-file TODO headers as a maturity signal.

3. Turn the new USB runtime into something dependable.
- Prioritize xHCI plus one real attached-device path over adding more breadth.
- Make deferred reenumeration and error recovery predictable under repeated runs.
- Keep `/proc/usb` truthful enough to debug runtime state without instrumenting the kernel every time.

4. Continue UI stabilization, not UI sprawl.
- Keep `6wm`/X11 bring-up moving, but prioritize crash/hang removal over new features.

## 31-60 Days (Capability Consolidation)

1. Make one USB path genuinely trustworthy.
- The obvious target is xHCI + RTL815x.
- Finish the missing control-plane work and stop pretending an attach path alone is enough.

2. Normalize networking driver depth.
- Keep virtio-net strong under the new IRQ model.
- Classify which NICs are real datapath drivers and which are still probe/bring-up class.

3. Codify storage and filesystem maturity.
- Publish an honest safe/unsafe matrix for storage backends and filesystems.
- Separate "mounts and works" from "safe to trust with data".

4. Push audio from single-path viability toward repeatable runtime behavior.

## 61-90 Days (Platform Credibility)

1. Make a threading decision and start landing it.
- Threading remains the largest platform-level missing capability.

2. Stabilize user-visible graphics contracts.
- Avoid growing userspace graphics expectations on shifting kernel behavior.

3. Standardize driver maturity reporting.
- Keep per-driver status visible: `solid`, `mixed`, `probe-only`, `experimental datapath`.

4. Harden the ports pipeline around what the kernel can really support.

## Project-Level Priorities (Ordered)

1. Correctness and reproducibility over feature-count optics.
2. Stabilize the subsystems that just moved fast: scheduler, PCI IRQ routing, USB runtime.
3. Use ports and real userland tools to force honest behavior.
4. Keep UI progress going, but not at the expense of kernel regression risk.
5. Label maturity explicitly so experimental paths are obvious.

## What "Done" Looks Like For This Roadmap Cycle

By the end of this cycle, success means:

1. MLFQ behavior is stable under repeated stress and runtime retuning.
2. MSI/MSI-X-capable drivers no longer regress each other as the PCI core evolves.
3. xHCI plus one attached USB device path is reliable enough to dogfood.
4. `6wm` session bring-up is repeatable enough for regular use.
5. Driver and filesystem maturity labels are explicit and trustworthy.

## Roadmap Maintenance Policy

To avoid drift:

1. Update this file from code and recent commits, not from inherited prose.
2. Keep a short "Changed Since Last Revision" section every time this file is touched.
3. When source-file banners disagree with the implementation, trust the implementation and fix the banner later.
4. Prefer removing stale roadmap claims over preserving optimistic ones.
