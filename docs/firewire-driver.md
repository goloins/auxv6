# FireWire / IEEE 1394 Driver Status (OHCI Scaffold)

This document tracks the current FireWire (IEEE 1394) kernel state in auxv6 and the next implementation slices.

## Scope

Current in-tree work targets PCI OHCI-1394 controller bring-up scaffolding only.

- Probe and match: PCI class/subclass/prog-if for IEEE 1394 OHCI.
- Minimal controller init: BAR mapping, register sanity checks, reset attempt, interrupt mask programming.
- Event handling: IRQ path and non-IRQ polled fallback path.
- Observability: rich runtime state in /proc/firewire.

Not yet in scope:

- Full async transaction engine.
- CSR/Configuration ROM transport surfaces.
- SBP-2 block transport.

## Current Implementation (2026-04-06)

Implemented in kernel/driver/firewire.c.

### Controller discovery and attach

- Boot-time scan of PCI devices for FireWire OHCI.
- BAR0 mapping and baseline OHCI register reads.
- Soft-reset attempt plus LinkEnable setup.
- Interrupt registration through shared IRQ framework when available.

### Phase/state machine

Per-controller phase tracking:

- init
- ready
- resetting
- degraded

Transitions are driven by attach outcomes and runtime events:

- init -> ready on successful post-attach path.
- any -> degraded on attach/init failures.
- ready -> resetting on bus reset.
- resetting -> ready on self-id complete.

### Generation guard and async context scaffold

A minimal async request-context ring is in place:

- Fixed-size per-controller context queue.
- Tokenized submit/complete bookkeeping.
- Generation-aware invalidation on bus reset.
- Timeout reaping for stale pending contexts.

This is intentionally a lifecycle scaffold, not a full packetized async request transport yet.

## /proc/firewire Telemetry

The /proc/firewire output includes:

- PCI identity and BDF placement.
- IRQ wiring state.
- Bus-reset and self-id counters.
- Current phase, phase-change count, and last phase-change tick.
- Event-path counters for IRQ and polled handling.
- Async queue health counters:
  - pending
  - submit
  - complete
  - stale (generation-invalidated)
  - timeout
  - oldest pending age (ticks)

These counters are intended to answer quickly:

- Is the controller alive and transitioning phases?
- Are resets being observed and settled?
- Is async context lifecycle progressing or stalling?

## Known Gaps

- No DMA descriptor programming for OHCI async transmit/receive contexts yet.
- No request/response matching machinery.
- No userspace ABI for raw1394-style operations yet.
- No SBP-2 target probing, login, or SCSI command transport.

## Next Tranches

1. Async engine skeleton:
   - descriptor/ring memory layout
   - submission doorbell path
   - completion decode and token matching
2. CSR/Configuration ROM baseline surfaces.
3. Raw control ABI (ioctl-level minimal control/inspect path).
4. SBP-2 read-only path and block-device integration.

## Manual Validation

Host:

- sudo make aux.kern
- sudo make qemu

Guest:

- cat /proc/firewire

Expected at this stage:

- Controller line(s) visible when hardware is present.
- Phase/counter fields present and parseable.
- No kernel panic when reading /proc/firewire repeatedly.
