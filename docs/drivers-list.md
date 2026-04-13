# auxv6 Driver Inventory And Driver-Model Plan

## Purpose

This document does two things:

1. Catalog the drivers and driver-like subsystems currently present in auxv6.
2. Define a concrete, phased plan for a new C-only driver model with a consistent integration and load path (IOKit-style architecture without C++ runtime requirements).

## Current Driver Integration Model (As-Is)

### Boot and attachment flow

Current boot-time initialization is explicit and mostly static. The main sequencing is in kernel/core/main.c and is function-call driven (not metadata driven). Important order points:

- Early platform and buses: mpinit, lapic/pic/ioapic, display_init, pci_init.
- Probe/scaffold classes next: modem_init, firewire_init, wifi_init, ieee802154_init, rtl815x_init, usb_init, virtio_gpu_init, intel_gfx_init.
- Character and terminal stack: consoleinit, ptyinit, serialinit, audio_init, tuntap_init, uartinit.
- Storage + network stack: bdevinit, ideinit, ahci_init, nvme_init, virtio_blk_init, loop_init, netdev_init, socket_init.

### Registration and hook mechanisms in use

There is no single unified driver-core object model yet. Integration currently happens through several subsystem-specific mechanisms:

- PCI enumeration/service table:
  - pci_init builds a global device table.
  - Most PCI drivers call pci_get_device in their own init and match manually.
  - pci_register_driver exists but is not the primary integration path used by most drivers today.
- IRQ binding:
  - Drivers typically call irq_register and pci_enable_irq directly.
- Character-device integration:
  - Drivers wire major handlers through devsw[major].read/write.
  - Active examples: console, pty, serial, audio, tuntap, blockdev frontend.
- Block-device integration:
  - Drivers register block ops through bdev_register/bdev_register_part.
- Network interface integration:
  - NIC drivers publish through if_register into the ifnet layer.
- Display integration:
  - Display-capable drivers can register display_device instances into graphics/display.
- Audio integration:
  - audio_core exposes ABI and stream machinery; audio probe modules register hardware identity/caps via audio_register_hw_device.

### Loading model

- Drivers are statically linked via Makefile object lists.
- There is no runtime module loader path today.
- Probe timing is mostly hard-coded in boot init order.

## Driver Inventory By Subsystem

Status legend:

- datapath: Driver has real I/O path integrated with core subsystem.
- attach: Driver can discover/attach and expose metadata, limited I/O or staged functionality.
- scaffold: Probe/identity/path scaffolding only; full datapath not implemented.

## Platform, bus, interrupt, and low-level core

| Area | Components | Status | Notes |
|---|---|---|---|
| PCI bus | driver/pci | attach | Enumerates devices, BAR helpers, capability helpers, IRQ mode helpers, static registry APIs. |
| Interrupt plumbing | driver/ioapic, driver/lapic, driver/picirq | datapath | Core interrupt routing and legacy/APIC path. |
| SMP/platform | driver/mp | datapath | Multiprocessor discovery/bootstrap support. |
| DMA services | driver/dma | datapath | DMA allocation path used by multiple drivers (including graphics/virtio flows). |
| Serial HW | driver/uart | datapath | UART bring-up and interrupt path. |

## Console, tty, input, pseudo devices

| Area | Components | Status | Notes |
|---|---|---|---|
| Console | driver/console | datapath | Console + graphics mirror logic, character major registration. |
| Line discipline | driver/tty_ldisc | datapath | TTY discipline layer for terminal semantics. |
| PTY | driver/pty | datapath | Dynamic PTY endpoints + devsw integration. |
| Serial chardev | driver/serial | datapath | TTY endpoint integration via major device. |
| Input devices | driver/kbd, driver/mouse | datapath | Keyboard/mouse event support used by console/graphics path. |
| Tun/Tap | driver/tuntap | datapath | Character device + ifnet registration path for virtual interfaces. |

## Storage and block

| Area | Components | Status | Notes |
|---|---|---|---|
| Block core | core/blockdev | datapath | bdev switch, partition indirection, BLOCKDEV major. |
| IDE/PATA | driver/ide | datapath | Legacy disk path + partition registration. |
| AHCI/SATA | driver/ahci | datapath | PCI attach, port init, IRQ/poll handling, block registration. |
| NVMe | driver/nvme | datapath | PCI attach, queue management, block registration. |
| Virtio block | driver/virtio + driver/virtio_blk | datapath | Virtio transport + block frontend integration. |
| Loop block devices | driver/loop | datapath | Loop mapping integrated with bdev layer. |
| Mem-backed IDE variant | driver/memide | datapath | Alternate memory-backed block path for memfs configurations. |

## Networking core and L3/L4 stack

| Area | Components | Status | Notes |
|---|---|---|---|
| Net core | net/device, net/route, net/socket | datapath | ifnet registry, routing, socket integration. |
| L2/L3/L4 | net/loopback, net/ethernet, net/arp, net/ip, net/icmp, net/udp, net/tcp | datapath | In-kernel protocol stack. |
| RPC/NFS transport | net/xdr, net/rpc, net/mount, net/nfs | attach | Networked filesystem transport path and client plumbing. |

## Ethernet and virtual NIC drivers

Datapath-capable ifnet publishers (registered by netdev_init):

- virtio_net
- e1000
- i219
- i226
- ax88179_pci
- pcnet
- rtl8111
- rtl8125
- rtl8139
- tg3
- bnxt
- atlantic
- skge
- via_rhine
- igb
- ixgbe
- i40e
- ice
- bnx2
- bnx2x
- mlx4_en
- mlx5e
- ena
- alx
- nforce
- vmxnet3
- netvsc
- loopback (virtual built-in interface)

Status summary: datapath for most PCI NIC families above, with maturity varying by driver family.

## USB subsystem

| Area | Components | Status | Notes |
|---|---|---|---|
| USB core orchestrator | driver/usb | attach | Host-controller discovery, lifecycle state, root-port scan/service scaffolding. |
| Host-controller backends | driver/usb_uhci, driver/usb_ohci, driver/usb_ehci, driver/usb_xhci | attach | Backend ops wired through common usb_hc_ops contract. |
| USB Ethernet family scaffold | driver/rtl815x | scaffold | USB attach identity/proc visibility; no full ifnet datapath yet. |
| USB 802.15.4 scaffold hook | driver/ieee802154 (USB attach pending) | scaffold | Planned subordinate-device attach path from USB core. |

## Audio subsystem

| Area | Components | Status | Notes |
|---|---|---|---|
| Audio core ABI + stream engine | audio/audio_core | attach | Character-device ABI, stream objects/rings/readiness/ioctl/proc plumbing. |
| Audio family orchestrator | driver/audio_pci, driver/audio_pci_common | attach | Family-level probe split and hardware registration into audio core. |
| AC97/HDA/legacy PCI families | audio_intel_ac97, audio_realtek_ac97, audio_creative_live, audio_creative_audigy, audio_cmedia_cm8738, audio_via_envy24, audio_yamaha_dsxg, audio_ess_maestro, audio_adi_soundmax, audio_sigmatel_hda, audio_intel_hda, audio_realtek_hda, audio_conexant_hda, audio_nvidia_mcp, audio_creative_xfi | attach | Mostly staged probe/identity integration with incremental backend depth. |

## Graphics/display

| Area | Components | Status | Notes |
|---|---|---|---|
| Display abstraction | graphics/display | attach | Registry and display-device ops dispatch. |
| Framebuffer/render foundation | graphics/framebuffer, graphics/font, graphics/render | attach | Generic framebuffer/raster infrastructure. |
| Virtio GPU | driver/virtio_gpu + driver/virtio | attach | Display-device integration, scanout/resource path, virtio transport. |
| Intel graphics probe | driver/intel_gfx | scaffold | PCI display-class discovery + MMIO mapping only. |

## Communications and specialty classes (probe-heavy)

| Area | Components | Status | Notes |
|---|---|---|---|
| FireWire | driver/firewire | scaffold | OHCI controller probe/state telemetry with partial control sequencing. |
| Wi-Fi | driver/wifi | scaffold | PCI 802.11 discovery and visibility scaffolding. |
| IEEE 802.15.4 | driver/ieee802154 | scaffold | WPAN subsystem scaffold, USB attach intended path. |
| Modem umbrella | driver/modem | scaffold | Aggregates modem-family probe stubs. |
| Modem families | driver/conexant_hsf, driver/agere_lt, driver/smartlink, driver/pctel, driver/intel_softmodem, driver/motorola_sm56 | scaffold | Family-specific probe visibility; datapath not complete. |

## Filesystem backend drivers (VFS-level)

These are not bus drivers, but they are driver-like filesystem backends integrated through VFS.

| Backend | Component | Status |
|---|---|---|
| xv6fs backend | fs/vfs_xv6fs | datapath (optional build path) |
| ext2 | fs/vfs_ext2 | datapath |
| msdosfs | fs/vfs_msdosfs | datapath |
| exfat | fs/vfs_exfat | attach |
| btrfs | fs/vfs_btrfs | attach |
| ufs2/ffs | fs/vfs_ufs2 | attach |
| isofs | fs/vfs_isofs | attach |
| tmpfs | fs/vfs_tmpfs | datapath |
| nfs VFS frontend | fs/vfs_nfs | attach |
| procfs | fs/procfs | datapath |

## Key Gaps In The Current Model

1. No single driver lifecycle contract across subsystems.
2. Driver initialization is hard-coded in main and subsystem init functions.
3. No uniform match/probe/bind/remove state machine.
4. No unified resource object (MMIO/PIO/IRQ/DMA) with common ownership/lifetime semantics.
5. No dependency-managed load order (beyond manual call order).
6. No standard mechanism for deferred probe and reprobe.
7. Driver state and observability are spread across custom /proc nodes, with no common schema.
8. Device-node policy and publication are per-subsystem, not centrally class-managed.

## Proposed New Driver Model (C-only, IOKit-style Concepts)

The target is a pure-C object model with explicit vtables/ops, no C++ ABI, no RTTI, and no exceptions.

## Target architecture concepts

1. Driver core objects
   - drv_bus: bus implementation (PCI, USB, platform, virtio bus view).
   - drv_device: discovered device node with parent/child links.
   - drv_driver: match/probe/remove callbacks plus metadata.
   - drv_class: class-level policy (net, block, tty, audio, display, input, misc).
2. Unified lifecycle
   - discovered -> matched -> probing -> bound -> active -> quiesced -> removed.
3. Unified resources
   - drv_resource entries: mmio, pio, irq, dma, gpio (future), clock (future).
   - Reference-counted acquisition/release and managed teardown helpers.
4. Deferred-probe engine
   - If a dependency is missing, probe returns defer and gets queued for replay.
5. Driver declaration mechanism
   - Link-time registration macros (for built-in drivers): DRIVER_DECLARE.
   - Optional future runtime module loader can reuse same ABI.
6. Class adapters
   - net class bridge to ifnet.
   - block class bridge to bdev.
   - char class bridge to devsw/devfs policy.
   - display class bridge to graphics/display.
   - audio class bridge to audio_core.
7. Standard observability
   - /proc/drv/devices, /proc/drv/drivers, /proc/drv/bindings, /proc/drv/events.

## Proposed source tree organization

Target end-state inside kernel:

- kernel/drivers/core
  - driver_core.c/.h
  - driver_match.c
  - driver_resource.c
  - driver_deferred.c
  - driver_event.c
- kernel/drivers/bus
  - pci/
  - usb/
  - virtio/
  - platform/
- kernel/drivers/class
  - net/
  - block/
  - char/
  - audio/
  - display/
  - input/
- kernel/drivers/vendor
  - intel/
  - realtek/
  - broadcom/
  - mellanox/
  - virtio/
  - misc/
- kernel/drivers/legacy
  - transitional wrappers for unmigrated drivers

Notes:

- Keep existing kernel/driver during migration; do not big-bang rename.
- Introduce compatibility headers and wrappers first, then move files subsystem by subsystem.

## Concrete Phased Plan

## Phase 0: Baseline capture and freeze

Goals:

- Freeze current attach graph and behavior before architectural changes.

Deliverables:

- Driver inventory snapshot (this document).
- Boot log golden files for representative hardware/VM profiles.
- Per-subsystem smoke checklist (storage, net, tty, usb, graphics, audio).

Exit criteria:

- Reproducible baseline pass/fail matrix is available.

## Phase 1: Driver core skeleton (non-invasive)

Goals:

- Add core types and lifecycle engine without migrating any existing driver logic.

Deliverables:

- New C interfaces: drv_bus, drv_device, drv_driver, drv_class.
- Global registries and lock discipline.
- Unified status/state enums and probe result codes (ok, fail, defer).
- Basic /proc/drv observability.

Exit criteria:

- Kernel boots with no functional regressions while core stays mostly unused.

## Phase 2: Bus adapters and resource API

Goals:

- Make PCI and USB publish drv_device objects, with resources normalized.

Deliverables:

- PCI bus adapter that wraps existing pci_dev instances into drv_device.
- USB host-controller and child-device adapter path into drv_device hierarchy.
- Resource API wrappers for BAR/IRQ/DMA acquisition and release.

Exit criteria:

- Existing probe logic can read resources through adapter shims.

## Phase 3: Class bridges (char/block/net/display/audio)

Goals:

- Define one integration contract per class, keeping existing subsystem internals intact.

Deliverables:

- net class bridge: standard driver callbacks map into ifnet registration.
- block class bridge: standard callbacks map into bdev registration.
- char class bridge: standard callback map into devsw sloting and node policy.
- display class bridge: standard callback map into display_device_register.
- audio class bridge: standard callback map into audio_register_hw_device plus stream backend ops.

Exit criteria:

- At least one driver per class uses new registration path successfully.

## Phase 4: Migrate low-risk drivers first

Goals:

- Prove migration approach with virtual and simple drivers before high-variance hardware.

Recommended migration order:

1. loop, tuntap, pty/serial helper layers (limited hardware risk)
2. virtio_blk, virtio_net, virtio_gpu (stable VM testability)
3. e1000 and one additional PCI NIC family
4. ahci and nvme adapters

Deliverables:

- DRIVER_DECLARE entries for migrated drivers.
- Legacy init call wrappers reduced to compatibility stubs.

Exit criteria:

- Equivalent functionality and performance in regression suites.

## Phase 5: Broad NIC and storage family migration

Goals:

- Move remaining production datapath families to driver-core model.

Deliverables:

- Full migration of PCI NIC families listed in netdev_init path.
- Full migration of IDE/AHCI/NVMe/virtio block paths to class registration.
- Unified interrupt setup through resource API wrappers.

Exit criteria:

- netdev_init and storage init become bus/class-driven, not manual per-driver call chains.

## Phase 6: USB subordinate model and scaffold-to-driver path

Goals:

- Replace ad hoc USB attach scaffolds with standard device-child binding.

Deliverables:

- USB child-device objects (vendor/product/class descriptors).
- rtl815x and ieee802154 moved to USB child-driver binding API.
- Deferred probe support used for devices awaiting controller readiness.

Exit criteria:

- USB family drivers bind through driver-core without special-case glue.

## Phase 7: Lifecycle hardening, power events, and fault handling

Goals:

- Add robustness expected of a modern driver model.

Deliverables:

- Quiesce/shutdown callbacks in drv_driver.
- Reset/recover hooks and standardized error counters.
- Driver health state in /proc/drv plus subsystem pass-through counters.

Exit criteria:

- Controlled unload/quiesce semantics for built-in shutdown/reinit flows.

## Phase 8: Optional runtime load/unload modules

Goals:

- Add dynamic loading only after static driver-core migration is stable.

Deliverables:

- Kernel module format/relocation policy.
- Signature/trust policy (even if simple allowlist initially).
- Load-time dependency checks against bus/class requirements.

Exit criteria:

- Modules can be loaded and bound through same drv_driver ABI as built-ins.

## Migration mechanics and governance

1. Compatibility-first policy
   - Keep old init entrypoints as wrappers during transition.
2. No big-bang rewrites
   - Migrate by class and hardware family.
3. Driver acceptance template
   - Each migrated driver must include: match table, probe path, resource map, IRQ strategy, class registration path, and rollback behavior.
4. Test gates per migration PR
   - Boot attach log diffs.
   - Subsystem functional tests.
   - Stress test for migrated class.
5. Telemetry parity requirement
   - Existing /proc observability must remain available until superseded by /proc/drv views.

## Immediate next implementation steps

1. Add kernel/drivers/core header and empty implementation with registries and states.
2. Implement a PCI-to-drv_device adapter without touching existing drivers.
3. Add net class bridge and migrate virtio_net first as pilot.
4. Add block class bridge and migrate virtio_blk second.
5. Convert main boot flow from manual per-driver calls to bus scan plus class binding for migrated drivers only.

This sequence provides fast proof that the new model works on real datapath drivers while minimizing risk to the rest of the kernel.
