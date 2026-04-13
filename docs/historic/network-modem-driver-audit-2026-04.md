# Network/Modem Driver Audit (2026-04)

## Scope

This audit enumerates in-tree network adapters and modem-family drivers, highlights support gaps, and defines implementation tranches.

Policy for this pass:
- Exclude VMware/Hyper-V adapters from required implementation work (`vmxnet3`, `netvsc`).
- Keep Linux/NetBSD/FreeBSD-inspired structure for small, low-risk increments.

## Network Adapter Inventory

### Full TX/RX datapath in-tree

- `virtio_net`
- `e1000`
- `i219`
- `i226`
- `ax88179_pci`
- `rtl8125`
- `rtl8139`
- `tg3`
- `bnxt`
- `atlantic`
- `skge`
- `via_rhine`
- `igb`
- `ixgbe`
- `i40e`
- `ice`
- `bnx2`
- `bnx2x`
- `mlx4_en`
- `mlx5e`
- `ena`
- `alx`

### Excluded in this directive (by request)

- `vmxnet3`
- `netvsc`

### Gap closure landed in this tranche

- `pcnet`: added `if_poll` hook to run RX/TX completion in polling path.
- `rtl8111`: added `if_poll` hook to run RX/TX completion in polling path.

These two drivers already had RX/TX completion logic, but did not wire `if_poll`, leaving packet progress dependent on IRQ-only behavior. Wiring poll parity follows established BSD/Linux behavior where poll loops service completion rings even when interrupts are masked/coalesced.

## Modem Driver Inventory

### Probe-only families in-tree

- `conexant_hsf`
- `agere_lt`
- `smartlink`
- `pctel`
- `intel_softmodem`
- `motorola_sm56`

### Gap closure landed in this tranche

- Added centralized modem probe registry in `kernel/driver/modem.c`.
- All modem-family probe stubs now register detected devices into that table.
- Added `/proc/modems` for runtime inventory and gap visibility.

`/proc/modems` format includes:
- family name
- `ven:dev`
- `class/subclass`
- `bus:slot.func`
- irq line
- status (`probe-only`)
- summary totals (`datapath=0`, `probe_only=N`)

This creates an explicit, queryable baseline for future modem implementation tranches.

## Remaining Gaps

### Network

- Link-state and media-status parity are still uneven across several non-Intel families.
- Many drivers remain polling-first and should eventually gain IRQ moderation/tuning where hardware supports it.
- Queue-depth/ring-size tuning is still mostly static.

### Network gap closure landed after initial audit

- Added normalized link-state field (`LINK_STATE_UNKNOWN/DOWN/UP`) to shared net ABI structs (`ifnet`, `netif_info`, `netifinfo`).
- `if_register()` now seeds link-state consistently from flags when drivers do not explicitly set it.
- `/proc/net_dev` now prints per-interface `Link` status.
- Userland interface views (`ifconfig`, `ip`, `netinfo` via `netcommon.h`) now show normalized link state.

### Modem

- No AT command engine.
- No PPP/SLIP data path.
- No per-family DSP/firmware/control-plane implementation.
- No userland modem diagnostics utility yet (beyond procfs and boot logs).

## Tranche Plan

1. Tranche N1 (landed): close missing polling hooks for active legacy NICs (`pcnet`, `rtl8111`).
2. Tranche N2 (partially landed): normalize link-state reporting path and diagnostics across all non-excluded NICs.
3. Tranche N3: per-driver ring/IRQ tuning pass (keep deterministic fallback to polling).
4. Tranche M1 (landed): modem probe registry + `/proc/modems` visibility.
5. Tranche M2: AT parser skeleton with software loopback backend and tty binding policy.
6. Tranche M3: optional PPP integration after AT/control plane is stable.
