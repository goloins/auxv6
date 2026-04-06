# Kernel Full Sweep (2026-04-06)

## Method
- Enumerated every file under kernel/.
- Flagged boundary-risk patterns per file:
  - direct user-string deref path (`fetchstr`, `argstr`)
  - raw ioctl arg deref (`*(type*)arg`, `(type*)arg` in ioctl handlers)
  - nested pointer-from-payload writes (e.g. `entries_ptr`)
- Non-source build artifacts (`.o`, `.d`) are listed but marked as not applicable.

## File Inventory and Flags

| File | Type | Risk Flags |
|---|---|---|
| kernel/audio/audio_core.c | c-source | raw-arg-deref/cast;nested-user-ptr; |
| kernel/audio/audio_core.d | build-artifact | none |
| kernel/audio/audio_core.o | build-artifact | none |
| kernel/boot/bootasm.S | asm-source | none |
| kernel/boot/bootmain.c | c-source | none |
| kernel/boot/entryother.S | asm-source | none |
| kernel/boot/initcode.S | asm-source | none |
| kernel/core/blockdev.c | c-source | none |
| kernel/core/blockdev.d | build-artifact | none |
| kernel/core/blockdev.o | build-artifact | none |
| kernel/core/entry.o | build-artifact | none |
| kernel/core/entry.S | asm-source | none |
| kernel/core/exec.c | c-source | none |
| kernel/core/exec.d | build-artifact | none |
| kernel/core/exec.o | build-artifact | none |
| kernel/core/kalloc.c | c-source | none |
| kernel/core/kalloc.d | build-artifact | none |
| kernel/core/kalloc.o | build-artifact | none |
| kernel/core/kmalloc.c | c-source | none |
| kernel/core/kmalloc.d | build-artifact | none |
| kernel/core/kmalloc.o | build-artifact | none |
| kernel/core/ktime.c | c-source | none |
| kernel/core/ktime.d | build-artifact | none |
| kernel/core/ktime.o | build-artifact | none |
| kernel/core/libgcc_compat.c | c-source | none |
| kernel/core/libgcc_compat.d | build-artifact | none |
| kernel/core/libgcc_compat.o | build-artifact | none |
| kernel/core/main.c | c-source | none |
| kernel/core/main.d | build-artifact | none |
| kernel/core/main.o | build-artifact | none |
| kernel/core/multiboot.c | c-source | none |
| kernel/core/multiboot.d | build-artifact | none |
| kernel/core/multiboot.o | build-artifact | none |
| kernel/core/pipe.c | c-source | none |
| kernel/core/pipe.d | build-artifact | none |
| kernel/core/pipe.o | build-artifact | none |
| kernel/core/proc.c | c-source | none |
| kernel/core/proc.d | build-artifact | none |
| kernel/core/proc.o | build-artifact | none |
| kernel/core/segreload.o | build-artifact | none |
| kernel/core/segreload.S | asm-source | none |
| kernel/core/sleeplock.c | c-source | none |
| kernel/core/sleeplock.d | build-artifact | none |
| kernel/core/sleeplock.o | build-artifact | none |
| kernel/core/spinlock.c | c-source | none |
| kernel/core/spinlock.d | build-artifact | none |
| kernel/core/spinlock.o | build-artifact | none |
| kernel/core/string.c | c-source | none |
| kernel/core/string.d | build-artifact | none |
| kernel/core/string.o | build-artifact | none |
| kernel/core/swtch.o | build-artifact | none |
| kernel/core/swtch.S | asm-source | none |
| kernel/core/syscall.c | c-source | fetchstr/argstr; |
| kernel/core/syscall.d | build-artifact | none |
| kernel/core/syscall.o | build-artifact | none |
| kernel/core/sysfile.c | c-source | fetchstr/argstr; |
| kernel/core/sysfile.d | build-artifact | none |
| kernel/core/sysfile.o | build-artifact | none |
| kernel/core/sysproc.c | c-source | none |
| kernel/core/sysproc.d | build-artifact | none |
| kernel/core/sysproc.o | build-artifact | none |
| kernel/core/trapasm.o | build-artifact | none |
| kernel/core/trapasm.S | asm-source | none |
| kernel/core/trap.c | c-source | none |
| kernel/core/trap.d | build-artifact | none |
| kernel/core/trap.o | build-artifact | none |
| kernel/core/vectors.o | build-artifact | none |
| kernel/core/vectors.S | asm-source | none |
| kernel/core/vm.c | c-source | none |
| kernel/core/vm.d | build-artifact | none |
| kernel/core/vm.o | build-artifact | none |
| kernel/driver/agere_lt.c | c-source | none |
| kernel/driver/agere_lt.d | build-artifact | none |
| kernel/driver/agere_lt.o | build-artifact | none |
| kernel/driver/ahci.c | c-source | none |
| kernel/driver/ahci.d | build-artifact | none |
| kernel/driver/ahci.o | build-artifact | none |
| kernel/driver/alx.c | c-source | none |
| kernel/driver/alx.d | build-artifact | none |
| kernel/driver/alx.o | build-artifact | none |
| kernel/driver/atlantic.c | c-source | none |
| kernel/driver/atlantic.d | build-artifact | none |
| kernel/driver/atlantic.o | build-artifact | none |
| kernel/driver/audio_adi_soundmax.c | c-source | none |
| kernel/driver/audio_adi_soundmax.d | build-artifact | none |
| kernel/driver/audio_adi_soundmax.o | build-artifact | none |
| kernel/driver/audio_cmedia_cm8738.c | c-source | none |
| kernel/driver/audio_cmedia_cm8738.d | build-artifact | none |
| kernel/driver/audio_cmedia_cm8738.o | build-artifact | none |
| kernel/driver/audio_conexant_hda.c | c-source | none |
| kernel/driver/audio_conexant_hda.d | build-artifact | none |
| kernel/driver/audio_conexant_hda.o | build-artifact | none |
| kernel/driver/audio_creative_audigy.c | c-source | none |
| kernel/driver/audio_creative_audigy.d | build-artifact | none |
| kernel/driver/audio_creative_audigy.o | build-artifact | none |
| kernel/driver/audio_creative_live.c | c-source | none |
| kernel/driver/audio_creative_live.d | build-artifact | none |
| kernel/driver/audio_creative_live.o | build-artifact | none |
| kernel/driver/audio_creative_xfi.c | c-source | none |
| kernel/driver/audio_creative_xfi.d | build-artifact | none |
| kernel/driver/audio_creative_xfi.o | build-artifact | none |
| kernel/driver/audio_ess_maestro.c | c-source | none |
| kernel/driver/audio_ess_maestro.d | build-artifact | none |
| kernel/driver/audio_ess_maestro.o | build-artifact | none |
| kernel/driver/audio_intel_ac97.c | c-source | raw-arg-deref/cast; |
| kernel/driver/audio_intel_ac97.d | build-artifact | none |
| kernel/driver/audio_intel_ac97.o | build-artifact | none |
| kernel/driver/audio_intel_hda.c | c-source | none |
| kernel/driver/audio_intel_hda.d | build-artifact | none |
| kernel/driver/audio_intel_hda.o | build-artifact | none |
| kernel/driver/audio_nvidia_mcp.c | c-source | none |
| kernel/driver/audio_nvidia_mcp.d | build-artifact | none |
| kernel/driver/audio_nvidia_mcp.o | build-artifact | none |
| kernel/driver/audio_pci.c | c-source | none |
| kernel/driver/audio_pci_common.c | c-source | none |
| kernel/driver/audio_pci_common.d | build-artifact | none |
| kernel/driver/audio_pci_common.h | header | none |
| kernel/driver/audio_pci_common.o | build-artifact | none |
| kernel/driver/audio_pci.d | build-artifact | none |
| kernel/driver/audio_pci.o | build-artifact | none |
| kernel/driver/audio_realtek_ac97.c | c-source | none |
| kernel/driver/audio_realtek_ac97.d | build-artifact | none |
| kernel/driver/audio_realtek_ac97.o | build-artifact | none |
| kernel/driver/audio_realtek_hda.c | c-source | none |
| kernel/driver/audio_realtek_hda.d | build-artifact | none |
| kernel/driver/audio_realtek_hda.o | build-artifact | none |
| kernel/driver/audio_sigmatel_hda.c | c-source | none |
| kernel/driver/audio_sigmatel_hda.d | build-artifact | none |
| kernel/driver/audio_sigmatel_hda.o | build-artifact | none |
| kernel/driver/audio_via_envy24.c | c-source | none |
| kernel/driver/audio_via_envy24.d | build-artifact | none |
| kernel/driver/audio_via_envy24.o | build-artifact | none |
| kernel/driver/audio_yamaha_dsxg.c | c-source | none |
| kernel/driver/audio_yamaha_dsxg.d | build-artifact | none |
| kernel/driver/audio_yamaha_dsxg.o | build-artifact | none |
| kernel/driver/ax88179_pci.c | c-source | none |
| kernel/driver/ax88179_pci.d | build-artifact | none |
| kernel/driver/ax88179_pci.o | build-artifact | none |
| kernel/driver/bnx2.c | c-source | none |
| kernel/driver/bnx2.d | build-artifact | none |
| kernel/driver/bnx2.o | build-artifact | none |
| kernel/driver/bnx2x.c | c-source | none |
| kernel/driver/bnx2x.d | build-artifact | none |
| kernel/driver/bnx2x.o | build-artifact | none |
| kernel/driver/bnxt.c | c-source | none |
| kernel/driver/bnxt.d | build-artifact | none |
| kernel/driver/bnxt.o | build-artifact | none |
| kernel/driver/conexant_hsf.c | c-source | none |
| kernel/driver/conexant_hsf.d | build-artifact | none |
| kernel/driver/conexant_hsf.o | build-artifact | none |
| kernel/driver/console.c | c-source | raw-arg-deref/cast; |
| kernel/driver/console.d | build-artifact | none |
| kernel/driver/console.o | build-artifact | none |
| kernel/driver/dma.c | c-source | none |
| kernel/driver/dma.d | build-artifact | none |
| kernel/driver/dma.o | build-artifact | none |
| kernel/driver/e1000.c | c-source | raw-arg-deref/cast; |
| kernel/driver/e1000.d | build-artifact | none |
| kernel/driver/e1000.o | build-artifact | none |
| kernel/driver/ena.c | c-source | none |
| kernel/driver/ena.d | build-artifact | none |
| kernel/driver/ena.o | build-artifact | none |
| kernel/driver/firewire.c | c-source | raw-arg-deref/cast; |
| kernel/driver/firewire.d | build-artifact | none |
| kernel/driver/firewire.o | build-artifact | none |
| kernel/driver/i219.c | c-source | none |
| kernel/driver/i219.d | build-artifact | none |
| kernel/driver/i219.o | build-artifact | none |
| kernel/driver/i226.c | c-source | none |
| kernel/driver/i226.d | build-artifact | none |
| kernel/driver/i226.o | build-artifact | none |
| kernel/driver/i40e.c | c-source | none |
| kernel/driver/i40e.d | build-artifact | none |
| kernel/driver/i40e.o | build-artifact | none |
| kernel/driver/ice.c | c-source | none |
| kernel/driver/ice.d | build-artifact | none |
| kernel/driver/ice.o | build-artifact | none |
| kernel/driver/ide.c | c-source | none |
| kernel/driver/ide.d | build-artifact | none |
| kernel/driver/ide.o | build-artifact | none |
| kernel/driver/ieee802154.c | c-source | none |
| kernel/driver/ieee802154.d | build-artifact | none |
| kernel/driver/ieee802154.o | build-artifact | none |
| kernel/driver/igb.c | c-source | none |
| kernel/driver/igb.d | build-artifact | none |
| kernel/driver/igb.o | build-artifact | none |
| kernel/driver/intel_gfx.c | c-source | none |
| kernel/driver/intel_gfx.d | build-artifact | none |
| kernel/driver/intel_gfx.o | build-artifact | none |
| kernel/driver/intel_softmodem.c | c-source | none |
| kernel/driver/intel_softmodem.d | build-artifact | none |
| kernel/driver/intel_softmodem.o | build-artifact | none |
| kernel/driver/ioapic.c | c-source | none |
| kernel/driver/ioapic.d | build-artifact | none |
| kernel/driver/ioapic.o | build-artifact | none |
| kernel/driver/ixgbe.c | c-source | none |
| kernel/driver/ixgbe.d | build-artifact | none |
| kernel/driver/ixgbe.o | build-artifact | none |
| kernel/driver/kbd.c | c-source | none |
| kernel/driver/kbd.d | build-artifact | none |
| kernel/driver/kbd.o | build-artifact | none |
| kernel/driver/lapic.c | c-source | none |
| kernel/driver/lapic.d | build-artifact | none |
| kernel/driver/lapic.o | build-artifact | none |
| kernel/driver/loop.c | c-source | none |
| kernel/driver/loop.d | build-artifact | none |
| kernel/driver/loop.o | build-artifact | none |
| kernel/driver/memide.c | c-source | none |
| kernel/driver/mlx4_en.c | c-source | none |
| kernel/driver/mlx4_en.d | build-artifact | none |
| kernel/driver/mlx4_en.o | build-artifact | none |
| kernel/driver/mlx5e.c | c-source | none |
| kernel/driver/mlx5e.d | build-artifact | none |
| kernel/driver/mlx5e.o | build-artifact | none |
| kernel/driver/modem.c | c-source | none |
| kernel/driver/modem.d | build-artifact | none |
| kernel/driver/modem.o | build-artifact | none |
| kernel/driver/motorola_sm56.c | c-source | none |
| kernel/driver/motorola_sm56.d | build-artifact | none |
| kernel/driver/motorola_sm56.o | build-artifact | none |
| kernel/driver/mp.c | c-source | none |
| kernel/driver/mp.d | build-artifact | none |
| kernel/driver/mp.o | build-artifact | none |
| kernel/driver/netvsc.c | c-source | none |
| kernel/driver/netvsc.d | build-artifact | none |
| kernel/driver/netvsc.o | build-artifact | none |
| kernel/driver/nforce.c | c-source | raw-arg-deref/cast; |
| kernel/driver/nforce.d | build-artifact | none |
| kernel/driver/nforce.o | build-artifact | none |
| kernel/driver/nvme.c | c-source | none |
| kernel/driver/nvme.d | build-artifact | none |
| kernel/driver/nvme.o | build-artifact | none |
| kernel/driver/pci.c | c-source | none |
| kernel/driver/pci.d | build-artifact | none |
| kernel/driver/pci.o | build-artifact | none |
| kernel/driver/pcnet.c | c-source | raw-arg-deref/cast; |
| kernel/driver/pcnet.d | build-artifact | none |
| kernel/driver/pcnet.o | build-artifact | none |
| kernel/driver/pctel.c | c-source | none |
| kernel/driver/pctel.d | build-artifact | none |
| kernel/driver/pctel.o | build-artifact | none |
| kernel/driver/picirq.c | c-source | none |
| kernel/driver/picirq.d | build-artifact | none |
| kernel/driver/picirq.o | build-artifact | none |
| kernel/driver/pty.c | c-source | raw-arg-deref/cast; |
| kernel/driver/pty.d | build-artifact | none |
| kernel/driver/pty.o | build-artifact | none |
| kernel/driver/rtl8111.c | c-source | raw-arg-deref/cast; |
| kernel/driver/rtl8111.d | build-artifact | none |
| kernel/driver/rtl8111.o | build-artifact | none |
| kernel/driver/rtl8125.c | c-source | none |
| kernel/driver/rtl8125.d | build-artifact | none |
| kernel/driver/rtl8125.o | build-artifact | none |
| kernel/driver/rtl8139.c | c-source | none |
| kernel/driver/rtl8139.d | build-artifact | none |
| kernel/driver/rtl8139.o | build-artifact | none |
| kernel/driver/serial.c | c-source | raw-arg-deref/cast; |
| kernel/driver/serial.d | build-artifact | none |
| kernel/driver/serial.o | build-artifact | none |
| kernel/driver/skge.c | c-source | none |
| kernel/driver/skge.d | build-artifact | none |
| kernel/driver/skge.o | build-artifact | none |
| kernel/driver/smartlink.c | c-source | none |
| kernel/driver/smartlink.d | build-artifact | none |
| kernel/driver/smartlink.o | build-artifact | none |
| kernel/driver/tg3.c | c-source | none |
| kernel/driver/tg3.d | build-artifact | none |
| kernel/driver/tg3.o | build-artifact | none |
| kernel/driver/tuntap.c | c-source | raw-arg-deref/cast; |
| kernel/driver/tuntap.d | build-artifact | none |
| kernel/driver/tuntap.o | build-artifact | none |
| kernel/driver/uart.c | c-source | none |
| kernel/driver/uart.d | build-artifact | none |
| kernel/driver/uart.o | build-artifact | none |
| kernel/driver/usb.c | c-source | none |
| kernel/driver/usb.d | build-artifact | none |
| kernel/driver/usb_ehci.c | c-source | none |
| kernel/driver/usb_ehci.d | build-artifact | none |
| kernel/driver/usb_ehci.o | build-artifact | none |
| kernel/driver/usb_hcd.h | header | none |
| kernel/driver/usb.o | build-artifact | none |
| kernel/driver/usb_ohci.c | c-source | none |
| kernel/driver/usb_ohci.d | build-artifact | none |
| kernel/driver/usb_ohci.o | build-artifact | none |
| kernel/driver/usb_uhci.c | c-source | none |
| kernel/driver/usb_uhci.d | build-artifact | none |
| kernel/driver/usb_uhci.o | build-artifact | none |
| kernel/driver/usb_xhci.c | c-source | none |
| kernel/driver/usb_xhci.d | build-artifact | none |
| kernel/driver/usb_xhci.o | build-artifact | none |
| kernel/driver/via_rhine.c | c-source | none |
| kernel/driver/via_rhine.d | build-artifact | none |
| kernel/driver/via_rhine.o | build-artifact | none |
| kernel/driver/virtio_blk.c | c-source | none |
| kernel/driver/virtio_blk.d | build-artifact | none |
| kernel/driver/virtio_blk.o | build-artifact | none |
| kernel/driver/virtio.c | c-source | none |
| kernel/driver/virtio.d | build-artifact | none |
| kernel/driver/virtio_gpu.c | c-source | raw-arg-deref/cast; |
| kernel/driver/virtio_gpu.d | build-artifact | none |
| kernel/driver/virtio_gpu.o | build-artifact | none |
| kernel/driver/virtio_net.c | c-source | none |
| kernel/driver/virtio_net.d | build-artifact | none |
| kernel/driver/virtio_net.o | build-artifact | none |
| kernel/driver/virtio.o | build-artifact | none |
| kernel/driver/vmxnet3.c | c-source | none |
| kernel/driver/vmxnet3.d | build-artifact | none |
| kernel/driver/vmxnet3.o | build-artifact | none |
| kernel/driver/wifi.c | c-source | none |
| kernel/driver/wifi.d | build-artifact | none |
| kernel/driver/wifi.o | build-artifact | none |
| kernel/fs/bio.c | c-source | none |
| kernel/fs/bio.d | build-artifact | none |
| kernel/fs/bio.o | build-artifact | none |
| kernel/fs/file.c | c-source | none |
| kernel/fs/file.d | build-artifact | none |
| kernel/fs/file.o | build-artifact | none |
| kernel/fs/fs.c | c-source | none |
| kernel/fs/fs.d | build-artifact | none |
| kernel/fs/fs.o | build-artifact | none |
| kernel/fs/log.c | c-source | none |
| kernel/fs/log.d | build-artifact | none |
| kernel/fs/log.o | build-artifact | none |
| kernel/fs/msdosfs.h | header | none |
| kernel/fs/procfs.c | c-source | none |
| kernel/fs/procfs.d | build-artifact | none |
| kernel/fs/procfs.o | build-artifact | none |
| kernel/fs/vfs_btrfs.c | c-source | raw-arg-deref/cast; |
| kernel/fs/vfs_btrfs.d | build-artifact | none |
| kernel/fs/vfs_btrfs.o | build-artifact | none |
| kernel/fs/vfs.c | c-source | none |
| kernel/fs/vfs.d | build-artifact | none |
| kernel/fs/vfs_exfat.c | c-source | raw-arg-deref/cast; |
| kernel/fs/vfs_exfat.d | build-artifact | none |
| kernel/fs/vfs_exfat.o | build-artifact | none |
| kernel/fs/vfs_ext2.c | c-source | none |
| kernel/fs/vfs_ext2.d | build-artifact | none |
| kernel/fs/vfs_ext2.o | build-artifact | none |
| kernel/fs/vfs_isofs.c | c-source | none |
| kernel/fs/vfs_isofs.d | build-artifact | none |
| kernel/fs/vfs_isofs.o | build-artifact | none |
| kernel/fs/vfs_msdosfs.c | c-source | raw-arg-deref/cast; |
| kernel/fs/vfs_msdosfs.d | build-artifact | none |
| kernel/fs/vfs_msdosfs.o | build-artifact | none |
| kernel/fs/vfs_nfs.c | c-source | none |
| kernel/fs/vfs_nfs.d | build-artifact | none |
| kernel/fs/vfs_nfs.o | build-artifact | none |
| kernel/fs/vfs.o | build-artifact | none |
| kernel/fs/vfs_tmpfs.c | c-source | none |
| kernel/fs/vfs_tmpfs.d | build-artifact | none |
| kernel/fs/vfs_tmpfs.o | build-artifact | none |
| kernel/fs/vfs_ufs2.c | c-source | none |
| kernel/fs/vfs_ufs2.d | build-artifact | none |
| kernel/fs/vfs_ufs2.o | build-artifact | none |
| kernel/fs/vfs_xv6fs.c | c-source | none |
| kernel/graphics/display.c | c-source | none |
| kernel/graphics/display.d | build-artifact | none |
| kernel/graphics/display.o | build-artifact | none |
| kernel/graphics/font.c | c-source | none |
| kernel/graphics/font.d | build-artifact | none |
| kernel/graphics/font.o | build-artifact | none |
| kernel/graphics/framebuffer.c | c-source | none |
| kernel/graphics/framebuffer.d | build-artifact | none |
| kernel/graphics/framebuffer.o | build-artifact | none |
| kernel/graphics/render.c | c-source | none |
| kernel/graphics/render.d | build-artifact | none |
| kernel/graphics/render.o | build-artifact | none |
| kernel/net/arp.c | c-source | none |
| kernel/net/arp.d | build-artifact | none |
| kernel/net/arp.o | build-artifact | none |
| kernel/net/buf.c | c-source | none |
| kernel/net/buf.h | header | none |
| kernel/net/device.c | c-source | none |
| kernel/net/device.d | build-artifact | none |
| kernel/net/device.h | header | none |
| kernel/net/device.o | build-artifact | none |
| kernel/net/ethernet.c | c-source | none |
| kernel/net/ethernet.d | build-artifact | none |
| kernel/net/ethernet.o | build-artifact | none |
| kernel/net/icmp.c | c-source | none |
| kernel/net/icmp.d | build-artifact | none |
| kernel/net/icmp.o | build-artifact | none |
| kernel/net/ip.c | c-source | none |
| kernel/net/ip.d | build-artifact | none |
| kernel/net/ip.h | header | none |
| kernel/net/ip.o | build-artifact | none |
| kernel/net/loopback.c | c-source | none |
| kernel/net/loopback.d | build-artifact | none |
| kernel/net/loopback.o | build-artifact | none |
| kernel/net/mount.c | c-source | none |
| kernel/net/mount.d | build-artifact | none |
| kernel/net/mount.o | build-artifact | none |
| kernel/net/nfs.c | c-source | none |
| kernel/net/nfs.d | build-artifact | none |
| kernel/net/nfs.o | build-artifact | none |
| kernel/net/route.c | c-source | none |
| kernel/net/route.d | build-artifact | none |
| kernel/net/route.o | build-artifact | none |
| kernel/net/rpc.c | c-source | none |
| kernel/net/rpc.d | build-artifact | none |
| kernel/net/rpc.o | build-artifact | none |
| kernel/net/socket.c | c-source | none |
| kernel/net/socket.d | build-artifact | none |
| kernel/net/socket.h | header | none |
| kernel/net/socket.o | build-artifact | none |
| kernel/net/tcp.c | c-source | none |
| kernel/net/tcp.d | build-artifact | none |
| kernel/net/tcp.o | build-artifact | none |
| kernel/net/udp.c | c-source | none |
| kernel/net/udp.d | build-artifact | none |
| kernel/net/udp.o | build-artifact | none |
| kernel/net/xdr.c | c-source | none |
| kernel/net/xdr.d | build-artifact | none |
| kernel/net/xdr.o | build-artifact | none |

## Triage Outcome

The pattern scan intentionally over-matches and is triaged below.

### Confirmed Boundary-Risk Files

- `kernel/core/syscall.c`
  - `fetchstr/argstr` model performs direct user-memory string dereference.
- `kernel/core/sysfile.c`
  - inherits `argstr/fetchstr` usage at syscall boundary.
- `kernel/audio/audio_core.c`
  - nested user pointer (`entries_ptr`) is cast and written directly in enum ioctl path.
- `kernel/driver/console.c`
  - tty ioctl backend dereferences pointer-shaped `arg` directly.
- `kernel/driver/pty.c`
  - tty ioctl backend dereferences pointer-shaped `arg` directly.
- `kernel/driver/serial.c`
  - tty ioctl backend dereferences pointer-shaped `arg` directly.
- `kernel/driver/tuntap.c`
  - ioctl backend API accepts pointer-shaped `arg`; currently staged by caller but contract remains raw-pointer style.

### Flagged But Not User-Boundary Violations (Reviewed)

- `kernel/driver/audio_intel_ac97.c`
- `kernel/driver/e1000.c`
- `kernel/driver/firewire.c`
- `kernel/driver/nforce.c`
- `kernel/driver/pcnet.c`
- `kernel/driver/rtl8111.c`
- `kernel/driver/virtio_gpu.c`

Reason: these casts are driver-private callback context (`void *arg` from IRQ/registration), not syscall user pointers.

- `kernel/fs/vfs_btrfs.c`
- `kernel/fs/vfs_exfat.c`
- `kernel/fs/vfs_msdosfs.c`

Reason: callback/context argument casts inside filesystem internals, not user-pointer ingress paths.

## Coverage Check

- Inventory entries captured from `find kernel -type f`: 414
- Report rows generated for `kernel/*`: 414

Conclusion: every file currently under `kernel/` is represented in this report.
