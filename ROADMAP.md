# auxv6 ROADMAP (2026-04-18)

## Why A New Roadmap

The previous roadmap became historical context instead of an operational plan.
This document resets the project direction from current code reality and recent
commit activity.

## Evidence And Trust Model

This roadmap is based on:

1. Live code in `kernel/`, `libc/`, `user/`, `ports/`, `include/`, and top-level build files.
2. Recent commit history (especially 2026-04-10 through 2026-04-18).
3. Active docs under `docs/` as supporting context.

Historic docs under `docs/historic/` are treated as non-authoritative unless cross-checked.
All key historic docs sampled for this pass were last touched in the same
"antiquate docs" commit on 2026-04-16, so they are used only as background,
not as source of truth.

## System Snapshot (Honest Assessment)

Legend:
- `Solid`: implemented and actively exercised
- `Mixed`: implemented with important known limits
- `Scaffold`: attach/probe paths exist, datapath incomplete

### 1) Kernel Internals

Status: `Mixed`, trending upward

What is solid:
- Scheduler has a fresh MLFQ implementation (5 queues, promotion/demotion,
  anti-starvation boost) and new schedperf validation tests for demotion,
  promotion, and boost behavior.
- VM/COW work is substantial and still actively refined.
- Process/descriptor infrastructure has moved beyond old fixed xv6-style limits.
- Procfs observability is broad and growing.

What is mixed/risky:
- VM remains a high-complexity area with recent redesign/revert history.
- Locking/concurrency remains an ongoing risk surface (recent lock-related fixes).
- Some interfaces still expose TODOs in core signal/job-control corners.

### 2) Filesystems And Storage

Status: `Mixed`

What is solid:
- ext2 and msdosfs are real and heavily used.
- AHCI/NVMe/virtio-blk are active and repeatedly tuned.
- Loop devices and ISO workflow exist and are test-backed.

What is mixed/scaffold:
- exFAT, Btrfs, and UFS2 are present but should be treated as staged maturity.
- NFS path is partially integrated; parts of RPC/NFS read-dir/decode remain stubbed.
- "Do not trust your data" warning in project culture is still appropriate.

### 3) Networking Stack And Drivers

Status: `Mixed` at stack level, `Mixed/Scaffold` at driver breadth level

What is solid:
- In-kernel Ethernet/IP/ARP/ICMP/UDP/TCP stack is in place.
- AF_UNIX, socketpair/sendmsg/recvmsg landed recently.
- Many PCI NIC drivers exist with practical datapaths.

What is mixed/scaffold:
- Driver breadth is large, but depth is uneven by family.
- Some drivers remain explicit TODO phase implementations.
- netvsc/vmxnet3 and several non-ethernet/network-adjacent classes remain partial.

### 4) Audio

Status: `Mixed`

What is solid:
- Audio core ABI and stream lifecycle are real.
- audiod/audiodctl/audiostat tooling exists.
- AC97 path is implemented enough for active bring-up and runtime control loops.

What is mixed/scaffold:
- Multi-client and true production policy path is still maturing.
- Capture/mixer/hardware-op completeness is not yet at parity with mature Unix audio stacks.
- Many PCI audio family drivers are probe/stub level.

### 5) Graphics, Display, And UI

Status: `Mixed`, currently a strategic focus

What is solid:
- Framebuffer/display/render/font layers are present.
- x6/x11 infrastructure exists and is under active maintenance.
- New `6wm` work is moving quickly.

What is mixed/scaffold:
- Graphics stack still has many TODO/stub sections (format conversion,
  mmap/unmap, some display policy controls).
- Intel graphics support is still probe-stub level.
- Recent commits explicitly call out 6wm instability cases (expected at this stage).

### 6) USB, Modem, Wi-Fi, WPAN, FireWire

Status: mostly `Scaffold` with targeted progress

What is solid:
- USB host-controller framework exists with UHCI/OHCI/EHCI/xHCI backends.

What is mixed/scaffold:
- Large parts of USB remain scaffold-level integration.
- Wi-Fi/WPAN/modem families are mostly probe/discovery scaffolds.
- FireWire has meaningful groundwork but remains a bring-up track.

### 7) libc / POSIX Surface

Status: `Mixed`, rapidly improving

What is solid:
- Big April push filled many portability holes.
- OpenSSH, Bash, LibreSSL, and p7zip porting pressure materially improved
  wrappers, headers, and syscall behavior.
- pthread surface exists as single-threaded stubs, which is explicit and honest.

What is mixed:
- Threading/runtime model remains a key missing capability.
- Some APIs are present as compatibility stubs and should remain clearly documented as such.

### 8) Userland Utilities, Admin Tools, And Tests

Status: `Solid` with continuing growth

What is solid:
- Very broad command/tool inventory now exists (core utils, net tools, admin,
  stress/perf tests, graphics tests, audio tools, account tooling).
- Stress and validation binaries (`schedperf`, `kallocstress`, `fsperf`, etc.) are
  central to development and are actively expanded.

What is mixed:
- Quality and edge-case maturity vary widely utility-to-utility.
- Some high-level user workflows still rely on unstable lower layers (WM/graphics/audio).

### 9) Ports

Status: `Mixed`, strategically valuable

What is solid:
- Ports are now a major correctness engine for kernel/libc hardening.
- Bash, Dash, LibreSSL, OpenSSH, p7zip, and UI ports are integrated in various levels.
- OpenSSH integration significantly improved auth/account, permissions, socket,
  and libc compatibility paths.

What is mixed:
- Ports structure and policy are still evolving.
- Some ports remain noisy/fragile in recipe and post-install behavior.

## Quantitative Signals From Current Tree

These are rough indicators from current source comments/markers, not strict bug counts.

- Stub/TODO marker density by top subsystem:
  - `kernel/driver`: 89
  - `kernel/graphics`: 22
  - `user`: 9
  - `libc`: 3
  - `kernel/fs`: 2
  - `kernel/net`: 2
  - `kernel/audio`: 2
  - `kernel/vm`: 2

Interpretation:
- Driver breadth is ahead of driver depth.
- Graphics remains actively under construction.
- Core FS/net/audio/vm have fewer marker comments, but still carry non-trivial architectural risk.

## 30 / 60 / 90 Day Plan

## Next 30 Days (Stability-First)

1. Lock in scheduler/VM correctness baseline
- Keep MLFQ tests green and expand regression checks around starvation, wakeups,
  and interactive responsiveness.
- Run repeated VM stress matrix and freeze known-good validation gates.

2. Finish OpenSSH-from-ports operational path
- Ensure sshd service lifecycle, account model, and config staging are repeatable.
- Keep kernel/libc fixes driven by real port behavior, not speculative checklists.

3. Harden UI bring-up loop
- Stabilize 6wm + x6 + x11 enough for repeatable session bring-up.
- Prioritize crashers/hangs over feature expansion.

4. Cleanly classify driver maturity
- Publish a per-driver maturity table (`datapath`, `mixed`, `probe-only`) and keep it updated.

## 31-60 Days (Capability Consolidation)

1. Audio milestone: from single-path runtime to robust multi-client policy
- Move from scaffold/partial policy toward production-grade audiod behavior.
- Improve capture/mixer/hw pointer truthfulness where currently approximated.

2. Networking milestone: normalize feature depth across active NIC families
- Add focused parity tests and reduce phase-gap behavior across drivers.
- Keep virtual and cloud-facing NICs on a realistic, explicit track.

3. USB milestone: convert host-controller scaffold into dependable device path
- Prioritize one reliable end-to-end class path first (then expand).

4. Filesystem milestone: codify safe/unsafe matrix
- Make maturity and data-safety expectations explicit for each backend.

## 61-90 Days (Platform Credibility)

1. Threading strategy decision and first implementation tranche
- Decide kernel/libc threading model and land minimum viable primitives.
- Convert pthread from placeholder to real capability incrementally.

2. Graphics ABI and userspace contracts
- Define stable user-visible graphics/device contracts before broad toolkit work.

3. Ports pipeline hardening
- Standardize recipe conventions, staging rules, and post-install behavior.
- Add CI-like repeatability scripts for key ports (bash, libressl, openssh).

4. Driver-core unification plan
- Start transitioning from ad-hoc per-subsystem bring-up to a clearer driver lifecycle model.

## Project-Level Priorities (Ordered)

1. Correctness and reproducibility over raw feature count.
2. Port-driven compatibility work as a forcing function for real Unix behavior.
3. UI progress, but not at the cost of kernel/libc regressions.
4. Explicit maturity labeling so users know what is experimental.

## What "Done" Looks Like For This Roadmap Cycle

By the end of this cycle, success means:

1. Scheduler/VM baseline is stable under repeated stress runs.
2. OpenSSH is boring to build, stage, configure, and run.
3. 6wm session bring-up is reliable enough for daily dogfooding.
4. Audio and USB each have one clearly documented, dependable end-to-end path.
5. Driver and filesystem maturity matrices are explicit and trustworthy.
6. Historical docs stay historical; operational planning is anchored in code + recent commits.

## Roadmap Maintenance Policy

To avoid repeat roadmap drift:

1. Update this file at least weekly while active development is ongoing.
2. Every roadmap claim should be traceable to code or recent commits.
3. If a section depends on historic docs, it must include a cross-check note.
4. Keep a short "changed since last roadmap update" section in future revisions.
