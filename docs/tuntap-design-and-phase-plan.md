# TUN/TAP Support Design and Phase Plan

## Purpose

This document is the implementation contract for adding full TUN/TAP support to auxv6.
It complements ROADMAP.md by defining concrete kernel/userspace architecture, compatibility policy, phased landing strategy, and validation gates.

## Directive and Policy Alignment

- Root filesystem and VFS policy remain unchanged. ext2 stays the primary and required target for validation flows.
- This work does not add or depend on xv6fs paths.
- The rollout is staged and fail-closed: unsupported features return deterministic errors instead of silent partial behavior.

## Scope

### In Scope

- Character device control endpoint at /dev/net/tun.
- Linux-compatible baseline ioctl subset for common tooling:
  - TUNSETIFF
  - TUNGETIFF
  - TUNSETPERSIST
  - TUNSETOWNER
  - TUNSETGROUP
- Interface mode support for both:
  - TUN (L3 packets)
  - TAP (L2 Ethernet frames)
- Poll/select readiness integration.
- Userspace management tooling.
- Observability and regression coverage.

### Out of Scope (for now)

- Multiqueue tun/tap.
- Network namespaces.
- eBPF/tc offload hooks.
- Advanced Linux-only flags not needed by practical auxv6 workflows.

## Current Baseline (Before This Tranche)

- No tun/tap major device and no /dev/net/tun node.
- ioctl path in sys_ioctl is primarily tty-routed, so non-tty virtual-net control does not yet work end to end.
- ifnet and mbuf infrastructure already exist and are suitable integration points.

## Compatibility Contract

### ioctl Contract (Baseline)

- TUNSETIFF: create or bind to a named virtual interface with mode/flags.
- TUNGETIFF: return active interface name/flags for the fd binding.
- TUNSETPERSIST: set persistence policy for unit lifetime.
- TUNSETOWNER/TUNSETGROUP: set control ownership policy.

### Flags Contract (Baseline)

- IFF_TUN and IFF_TAP are mutually exclusive mode selectors.
- IFF_NO_PI supported for Linux-compatible no-extra-header behavior.
- Unsupported flag combinations fail with -1 (no silent downgrade).

### Error Policy

- Unsupported ioctl or unsupported mode/flag combination returns -1.
- Invalid fd/device type returns -1.
- Invalid argument pointers/sizes return -1.
- Unimplemented behavior in early phases fails predictably and safely.

## Architecture

### Device Plane

- Major device: TUNTAPDEV.
- Control endpoint: /dev/net/tun (minor 0).
- Kernel driver: kernel/driver/tuntap.c.
- devsw registration:
  - read: packet dequeue from kernel->userspace queue
  - write: packet enqueue from userspace->kernel ingress path

### Data Plane

- TUN mode:
  - userspace write -> L3 ingress (IP path)
  - kernel egress from if_output -> userspace read queue
- TAP mode:
  - userspace write -> L2 ingress via ether_input
  - kernel egress Ethernet frames -> userspace read queue

### Interface Plane

- Dynamic ifnet allocation and registration per virtual unit.
- Virtual link state and MTU policy exposed through existing interface reporting paths.
- Route/ARP behavior uses existing stack contracts.

### Control Plane

- sys_ioctl gains non-tty device ioctl routing path for tun/tap requests.
- tty/audio behavior remains unchanged and routed exactly as before.

### Readiness Plane

- poll/select ready bits sourced from tun/tap queue state.
- No busy loops; readiness reflects queue occupancy/capacity.

## Phased Delivery Plan

### Phase 0: ABI and Control-Plane Scaffolding

Deliverables:

- ioctl constants and baseline ifreq ABI surface.
- tun/tap major assignment and driver skeleton.
- sys_ioctl non-tty dispatch path for tun/tap requests.
- devman creation of /dev/net/tun.
- open/close/poll integration hooks in file/syscall paths.

Definition of Done:

- Kernel builds clean with scaffold enabled.
- /dev/net/tun appears via devman scan.
- Placeholder ioctl/data-path behavior fails deterministically.
- Existing tty/audio ioctl flows are unchanged.

### Phase 1: TUN L3 Functional Path

Deliverables:

- fd-to-unit binding and lifetime tracking.
- TUNSETIFF/TUNGETIFF functional for tun mode.
- L3 ingress/egress queue flow operational.
- blocking/nonblocking read/write semantics for tun fds.
- poll/select readiness based on queue state.

Definition of Done:

- tun0 create/configure/route/inject/receive workflow succeeds.
- No regressions in existing network stack smoke tests.

### Phase 2: TAP L2 Functional Path

Deliverables:

- TAP mode binding and frame path.
- Ethernet ingress to ether_input and egress to userspace queue.
- ARP + IPv4 validation through tap interfaces.

Definition of Done:

- tap0 frame exchange works with ARP + ICMP flow.
- Deterministic frame validation and drop accounting.

### Phase 3: Ownership and Persistence Hardening

Deliverables:

- owner/group policy enforcement.
- persistence semantics for reopen/delete behavior.
- robust close, process-exit cleanup, and leak-free teardown.

Definition of Done:

- stress loops show stable lifecycle behavior with no leaks.

### Phase 4: Tooling and Observability

Deliverables:

- userspace management UX (ip tuntap style create/delete/up/down).
- ifconfig/netinfo/proc visibility for mode and counters.
- manpage + compatibility matrix.

Definition of Done:

- end-to-end management and diagnostics are reproducible using in-tree tooling.

### Phase 5: Regression and Soak Signoff

Deliverables:

- tuntest regression utility.
- scripted tun and tap smoke suites.
- mixed-interface soak with existing network tests.

Definition of Done:

- sustained green regression matrix with tun/tap enabled.

## Phase 0 Implementation Notes (Started)

Initial scaffold has been added for:

- TUNTAPDEV major definition and /dev/net/tun devman enumeration.
- include/sys/ioctl.h baseline tun/tap ioctl constants and ifreq struct.
- kernel/driver/tuntap.c skeleton with devsw registration and placeholder handlers.
- sys_ioctl routing hook for tun/tap ioctl requests before tty-only routing.
- open/close and poll integration hook points.

Current phase intent is structural correctness and safe failure behavior, not datapath readiness.

### Phase 0 status snapshot (2026-04-05)

Implemented now:

- New driver scaffold is compiled and initialized at boot.
- /dev/net/tun is created by devman policy scan.
- ioctl routing now supports tun/tap requests on non-tty fds through a dedicated dispatch path.
- Minimal unit/session state is functional:
  - TUNSETIFF can bind an fd to a tun/tap unit.
  - TUNGETIFF returns mode/name for the bound unit.
  - TUNSETPERSIST, TUNSETOWNER, and TUNSETGROUP are enforced by ownership checks (owner-or-root policy for admin operations).
- open/close and poll/select hook points are wired.
- Named-create semantics are hardened:
  - explicit names are validated,
  - mode-prefix mismatches (`tun*` vs `tap*`) are rejected,
  - existing-name bind checks owner policy,
  - missing explicit names can allocate new units.
- A minimal userspace control utility is in-tree (`tuntapctl`) for create/get/persist/owner/group operations.

Intentional limitations still present in Phase 0:

- Control-plane semantics are implemented, but no advanced Linux extensions beyond the baseline ioctl subset are exposed.
- TAP packet ingress path is still deferred (Phase 2 target).

### Phase 1 completion snapshot (2026-04-05)

Completed now:

- Per-unit ifnet registration is wired during successful `TUNSETIFF` binding.
- Kernel egress path enqueues frames into per-unit queues through ifnet `if_output`.
- Tun file descriptors now have a queue-backed read path (`fileread`) and tun-mode write ingress (`filewrite`) into the IP input path.
- `O_NONBLOCK` is now tracked for tun file descriptors via `fcntl(F_SETFL/F_GETFL)` and respected by queue reads.
- Nonpersistent close semantics now unregister tun interfaces from the ifnet list and release their unit state, so destroy/teardown behavior is testable instead of purely cosmetic.
- An in-tree regression utility now exists (`tuntest`) covering empty nonblocking reads, empty-queue poll readiness, and an end-to-end tun ICMP self-test that injects an echo request and validates the queued reply.
- Guest validation confirms end-to-end behavior: create/configure/test/destroy on `tun0` passes, including `tuntest run-all` and interface removal from `ifconfig` plus `/proc/net_dev` after destroy.

Phase 1 tranche-start limitations (historical context):

- No TAP ingress handling yet (writes are tun-only for now).
- Queue model is intentionally simple (single queue, fixed depth, no multiqueue).
- Regression coverage is still first-pass only: no soak loops, mixed-interface stress, or TAP/L2 validation yet.

This closes the intended Phase 1 objective: real tun data path foundations with verified teardown behavior.

### Phase 2 tranche start snapshot (2026-04-05)

Started now:

- TAP userspace write path now accepts Ethernet frames and injects them into `ether_input`.
- TAP ingress has minimum-frame validation at driver boundary (rejects frames shorter than Ethernet header length).
- `tuntest` now includes a TAP ARP self-test (`tap-arp-self`) that:
  - writes a broadcast ARP request frame into TAP,
  - waits for and validates kernel ARP reply frame fields,
  - verifies `/proc/net_dev` counter advancement.
- `tuntest run-all-tap` provides first-pass TAP regression sequencing (empty nonblock + empty poll + ARP self-test).

Current Phase 2 limitations (expected at tranche start):

- TAP validation is currently ARP-focused; broader IPv4 forwarding/bridge-like scenarios remain follow-on.
- No soak/stress loops yet for TAP under sustained load.
- Queue model remains single-queue/fixed-depth.

### Phase 2 guest validation checkpoint (2026-04-05)

Validated in guest:

- `devman -s` remains idempotent before TAP tests.
- `tuntapctl create tap tap0` succeeds and interface state is visible via `ifconfig`.
- `tuntest tap-arp-self tap0 <local-ip> <peer-ip>` passes.
- `tuntest run-all-tap tap0 <local-ip> <peer-ip>` passes end-to-end.
- `/proc/net_dev` counters advance as expected during TAP self-tests.
- `tuntapctl destroy tap0` removes the interface from both `ifconfig` and `/proc/net_dev`.

## Risk Register

- ioctl dispatch regression risk:
  - Mitigation: route tun/tap only when request is recognized and fd is verified as TUNTAPDEV.
- lock-order risk as data-path queues are introduced:
  - Mitigation: keep lock scope narrow and rank-annotated; add lock-aware tests in later phases.
- regressions in poll/select semantics:
  - Mitigation: dedicated readiness tests and mixed descriptor-type poll coverage.
- management UX drift from Linux expectations:
  - Mitigation: explicit compatibility matrix and fail-fast policy for unsupported requests.

## Validation Matrix (Target)

- Build:
  - make aux.kern
- Device node:
  - devman -s creates /dev/net/tun
- ioctl scaffolding:
  - known requests route to tuntap driver
  - unknown requests fail with -1
- Non-regression:
  - existing tty/audio ioctl paths remain functional
  - existing net smoke utilities remain functional

## Implementation Backlog by File (Planned)

- include/file.h
- include/sys/ioctl.h
- include/defs.h
- kernel/driver/tuntap.c
- kernel/core/sysproc.c
- kernel/core/sysfile.c
- kernel/fs/file.c
- kernel/core/main.c
- user/devman.c
- user/ip.c (or new tuntap utility)
- user/ifconfig.c and user/netinfo.c (mode/counter visibility follow-on)
- docs/man pages and regression scripts

## Change Control

Every phase landing should update:

- docs/ROADMAP.md phase status line
- this document phase status and completed deliverables
- repository memory note with validated behavior and known gaps
