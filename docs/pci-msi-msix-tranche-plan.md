# PCI MSI/MSI-X Tranche Plan

This document tracks staged bring-up after landing kernel-side prerequisites.

## Scope And Goal

Goal: move auxv6 PCI interrupt handling from INTx-default to MSI/MSI-X-first where supported, while preserving stable fallback to INTx.

Landed kernel prerequisites:
- PCI capability discovery helper (`pci_find_capability`).
- Generic PCI IRQ vector API with fallback chain (`MSI-X -> MSI -> INTx`).
- MSI/MSI-X programming support in PCI core.
- High-vector IRQ dispatch expansion in trap path for MSI/MSI-X delivery.

Out of scope for this tranche set:
- Full interrupt affinity balancing.
- Per-vector `/proc` affinity controls.
- MSI remapping/IOMMU-level policy.

## Kernel API Contract (Now Available)

- `pci_irq_alloc_vectors(dev, min, max, flags)`:
  - Requests interrupt vectors with policy flags.
  - Attempts MSI-X first, then MSI, then INTx (depending on flags).
- `pci_irq_vector(dev, idx)`:
  - Returns trap IRQ index for `irq_register`.
- `pci_irq_mode(dev)`:
  - Returns current mode (`INTx`, `MSI`, `MSI-X`).
- `pci_irq_free_vectors(dev)`:
  - Releases MSI/MSI-X allocations and returns to INTx baseline.

Driver-facing rule:
- Register handlers using `pci_irq_vector(dev, i)`, not `dev->irq_line`.

## Tranche 0: Guardrails And Observability

1. Add lightweight boot/debug logging around mode choice per device class.
2. Extend `/proc/pci` formatting to optionally include interrupt mode and vector count.
3. Add a focused kernel self-check path:
   - Validate vector allocations never overlap reserved low vectors.
   - Validate teardown returns vector slots.

Exit criteria:
- No regressions in existing INTx drivers.
- Logs clearly show fallback path when MSI/MSI-X are unavailable.

## Tranche 1: Single-Vector Driver Conversions

Targets (single IRQ today):
- `virtio_blk`
- `virtio_net`
- `e1000`
- `pcnet`
- `rtl8111`
- `ahci`

Per-driver migration pattern:
1. Replace legacy `dev->irq_line` registration with `pci_irq_alloc_vectors(dev, 1, 1, PCI_IRQ_F_ALL)`.
2. Register one handler on `pci_irq_vector(dev, 0)`.
3. Keep legacy operational behavior unchanged (no queue topology changes yet).
4. On remove/failure paths, call `pci_irq_free_vectors(dev)`.

Exit criteria:
- Drivers boot and operate with MSI or MSI-X where available.
- Explicit fallback to INTx remains functional.
- Existing functional tests stay green.

## Tranche 2: MSI-X Multi-Vector Adoption

Targets (multi-queue/high-throughput first):
- `nvme`
- `ixgbe`
- `i40e`
- `ice`
- `bnx2x`
- `mlx4_en` / `mlx5e`

Per-driver enhancements:
1. Request multiple vectors (`min >= 2` where useful).
2. Split admin/control and data-path IRQ handling.
3. Map queues/CQs to vectors; preserve single-vector fallback path.
4. Add conservative unmask sequencing after handler install.

Exit criteria:
- Stable under RX/TX/storage stress.
- No unhandled-vector traps.
- Driver reset/recovery paths re-program vectors reliably.

## Tranche 3: Reliability Hardening

1. Reset/reprobe safety audit for all converted drivers.
2. Suspend-like teardown/re-init path validation (where relevant in auxv6 model).
3. IRQ storm resilience checks and bounded handler behavior.
4. Optional affinity plumbing for future scaling.

Exit criteria:
- Repeated attach/detach or reset cycles remain stable.
- No leaked vector allocations.
- No stale MSI/MSI-X enable state after driver teardown.

## Validation Matrix

Host/build:
- `make aux.kern`

Guest/manual (run by user):
1. Boot normally and inspect attach logs for mode choice.
2. Exercise storage/network paths for converted drivers.
3. Repeat initialization cycles to confirm teardown correctness.

Suggested stress points:
- sustained network TX/RX
- repeated block I/O bursts
- repeated mount/unmount where applicable

## Follow-On (Post Tranche 3)

- Expose interrupt mode/vector details in additional procfs diagnostics.
- Add optional per-driver policy knobs for forcing INTx/MSI/MSI-X during bring-up.
- Add queue-to-vector affinity controls when SMP scaling is prioritized.
