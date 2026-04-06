# USB Subsystem Progress (2026-04)

This document tracks the USB host-controller bring-up work that moved from early scaffold to first real per-port state handling.

## Current Snapshot

- Status: 65% (see roadmap)
- Scope landed: controller discovery, lifecycle control, root-hub port scan, post-scan port service/reset, candidate-device synthesis, and backend speed decoding
- Not yet landed: transfer scheduling, endpoint-0 control transfers, address assignment, descriptor parsing, class drivers

## Tranche History

1. PCI discovery + procfs baseline
- Added USB controller discovery over PCI (`UHCI/OHCI/EHCI/xHCI` + unknown)
- Added `/proc/usb` with per-controller identity and BAR telemetry

2. Phase and register sanity
- Added per-controller phase tracking (`init/ready/degraded`)
- Added conservative BAR0 register probe fields (`caplen`, `hciver`, `reg0`, `reg1`)

3. Backend split
- Split core from backend ops in `usb_hcd.h`
- Added dedicated backend files: `usb_uhci.c`, `usb_ohci.c`, `usb_ehci.c`, `usb_xhci.c`

4. Lifecycle hooks and error accounting
- Added `probe/reset/halt/start` hooks per backend
- Added explicit init-failure/error-state accounting in core and procfs

5. Real EHCI control path
- Implemented real `USBCMD/USBSTS` sequencing for halt/reset/start with bounded polling

6. Real xHCI control path
- Implemented real xHCI halt/reset/start with bounded polling and `CNR` readiness wait

7. Root-hub port scan
- Added `scan_ports` pass after controller start
- Captured per-port connect and change bitmaps (`rh_connect_bits`, `rh_change_bits`)

8. Port service/reset stage
- Added `service_ports` pass after scan
- Per-port reset/enable handling added in all four backends
- Added `rh_enabled_bits` plus service counters in procfs (`sv=`)

9. Candidate-device synthesis
- Added first-pass candidate table derived from root-hub state
- `/proc/usb` now prints `usb_dev_count` and `devN` rows:
  - `hc`, `kind`, `port`, `present`, `enabled`, `speed`, `addr_ready`

10. Speed decoding
- Added per-controller speed bitmasks:
  - `rh_low_bits`, `rh_full_bits`, `rh_high_bits`, `rh_super_bits`
- Backend decode rules:
  - UHCI/OHCI: LSDA -> low/full
  - EHCI: enabled ports treated as high-speed
  - xHCI: Port Speed ID -> low/full/high/super
- Candidate rows now report decoded speed instead of always unknown

## Procfs Surface (Current)

`/proc/usb` now exports:
- Controller counts and phase counts
- Per-controller lifecycle counters: `p`, `r`, `h`, `s`
- Scan counters: `sc`
- Service counters: `sv`
- Root-hub bitmaps:
  - `rh_connect`, `rh_change`, `rh_enabled`
  - `rh_low`, `rh_full`, `rh_high`, `rh_super`
- Candidate-device table:
  - `usb_dev_count`
  - `devN` rows with `addr_ready` handoff signal

## Implementation Notes

- Poll loops are bounded; no unbounded waits in controller lifecycle/service paths
- Backend-specific register semantics are isolated in backend files
- Core sequencing remains conservative and monotonic:
  - `probe -> reset -> halt -> start -> scan_ports -> service_ports -> collect_candidates`

## Next Recommended Tranches

1. Address assignment scaffold
- Reserve unique USB addresses (`1..127`) for `addr_ready` candidates
- Persist assigned address in candidate/device records
- Export allocator counters (`assigned`, `conflict`, `exhausted`) in `/proc/usb`

2. EHCI endpoint-0 control transfer MVP
- Minimal QH/qTD setup for `GET_DESCRIPTOR(Device)`
- Read first 18 bytes and publish `vid/pid/class/subclass/protocol`

3. Stable device identity model
- Add generation key `(hc, port, connect-change epoch)`
- Prevent stale candidate reuse across disconnect/reconnect transitions
