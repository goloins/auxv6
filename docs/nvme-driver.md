# NVMe Driver — Design, Debug Notes, and Known Issues

## Overview

`kernel/driver/nvme.c` implements a polled NVMe 1.x driver for auxv6.  It
supports a single I/O queue pair on the first namespace of each detected
controller, registered as a block device (device class `ND_DISK`).

### Architecture

| Layer | File | Role |
|---|---|---|
| PCI probe | `kernel/driver/pci.c` | Enumerates PCI class 0x01 sub 0x08 |
| NVMe driver | `kernel/driver/nvme.c` | Controller init, identify, I/O |
| Block device | `kernel/driver/blockdev.c` | `bdev_register` / `bdev_rw` |
| VFS | `kernel/fs/vfs.c` | `open`, `read`, `write` via block layer |

### Polled I/O

The driver is entirely polled — no IRQ handler is registered.  All NVMe
completions are detected by spinning on the CQ phase bit (`cq_phase`).  The
poll loop times out after `nvme_cmd_timeout_us` microseconds (default 1 s).

### Queue Sizing

| Queue | Depth | SQ size | CQ size |
|---|---|---|---|
| Admin | 64 | 64 × 64 B = 4096 B | 64 × 16 B = 1024 B |
| I/O (each) | 64 | 64 × 64 B = 4096 B | 64 × 16 B = 1024 B |

Both SQ and CQ for each queue occupy exactly one `kalloc()` page (4096 B) or
less.  **Do not raise `NVME_IO_QUEUE_SIZE` above 64 without switching to a
multi-page allocator** — the SQ would overflow a single page.

### Monotonic CID

Command IDs are allocated from `sc->next_cid`, an incrementing counter that
wraps at 0xFFFF and skips 0 (reserved).  This prevents stale CQE matches when
completions arrive out of order.

---

## QEMU Test Targets

### Images

| Target | Size | FS | Notes |
|---|---|---|---|
| `nvme-ext2.img` | 32 MB | ext2 | Built by `mke2fs` |
| `nvme-fat.img` | 16 MB | FAT16 | Built by `mkdosfs`/`newfs_msdos` |

### QEMU Targets

| Make target | Console | Image |
|---|---|---|
| `qemu-nvme` | GFX | `nvme-ext2.img` |
| `qemu-nox-nvme` | serial | `nvme-ext2.img` |
| `qemu-nvme-fat` | GFX | `nvme-fat.img` |

Run with `sudo make qemu-nvme` (root needed for TAP networking on macOS).

### Mount test sequence (inside guest)

```sh
# Either dev number or /dev name works
mount 40 ext2fs /mnt
# or equivalently:
mount /dev/nda ext2fs /mnt
lsblk
ls /mnt
mounts
umount /mnt
```

Both forms have been confirmed working (2026-04-03).  The kernel `sys_mount`
accepts `"ext2"` and `"ext2fs"` as equivalent fstype strings.

---

## Device Number Layout

Block device numbers in auxv6 are computed from constants in `include/fcntl.h`.
The table below shows the static allocation as of 2026-04-03 (`NDEV=64`).

| Range | Count | Macro | Driver | Dev names |
|-------|-------|-------|--------|-----------|
| 0–3   | 4  | `HD_DISK_DEV(u)` | IDE disk (`hd`) | `hda`–`hdd` |
| 4–19  | 16 | `HD_PART_DEV(u,p)` | IDE partitions | `hda1`–`hdd4` |
| 20–23 | 4  | `VD_DISK_DEV(u)` | Virtio-blk disk (`vd`) | `vda`–`vdd` |
| 24–39 | 16 | `VD_PART_DEV(u,p)` | Virtio-blk partitions | `vda1`–`vdd4` |
| 40–43 | 4  | `ND_DISK_DEV(u)` / `ND_DISK_BASE` | NVMe disk (`nd`) | `nda`–`ndd` |
| 44–51 | 8  | `LOOP_DEV_BASE + u` | Loop devices | `loop0`–`loop7` |
| 52–63 | 12 | — | _unassigned_ | — |

`/dev/nda` is always dev=40.  `NDEV` in `include/param.h` must remain ≥ 52 to
cover all allocated numbers.

### History — Loop/NVMe Collision (fixed 2026-04-03)

`LOOP_DEV_BASE` was originally hardcoded `40` in `kernel/driver/loop.c`,
identical to `ND_DISK_BASE`.  Loop `init()` runs after NVMe probe in `main.c`,
so `bdev_register(40..47, &loop_bdevsw)` silently overwrote the NVMe entry and
zeroed `nblocks`.  The symptom was `lsblk` not showing `nda` despite
`nvme: registered dev=40 blocks=131072` appearing in the boot log.

Three sites were updated:

| File | Change |
|------|--------|
| `kernel/driver/loop.c` | `LOOP_DEV_BASE` 40 → 44 |
| `user/mount.c` | `parse_dev_token` loopN → 44+N |
| `user/devman.c` | loop enumeration `dev = 44 + unit` |

---

## Bootloop Debug — Root Cause

### Symptom (first test run)

```
nvme: found at 0:2.0 regs=febc0000
nvme: cap=0x400820f0107ff mqes=2047 dstrd=4
nvme: controller enabled
[reboot]
```

The system rebooted immediately after "controller enabled" with no further
output, suggesting a triple fault.

### What succeeded

- PCI enumeration (device found at 0:2.0)
- BAR0 mapping
- `pci_enable_mem` / `pci_set_master`
- Controller disable → CAP read → enable sequence — CSTS.RDY went high

### Crash window

The current `DBG_NVME` trace places the crash after the first IDENTIFY command
is prepared and just before queue submission enters the lock-protected path:

- reset completes
- admin queue is allocated
- controller enable completes and RDY is set
- `id_ctrl` allocation succeeds
- IDENTIFY CONTROLLER is prepared
- reboot occurs before the first `nvme_submit_cmd()` trace appears

With the current `DBG_NVME` tracepoints enabled, this window can now be split
into smaller checkpoints:

- before/after CC=0 reset
- before/after AQA, ASQ, and ACQ programming
- before CC write
- after RDY becomes set
- before `id_ctrl` allocation
- before IDENTIFY CONTROLLER submission
- after interrupt mask re-application (`enable intms=...`)
- at the first queue-lock acquisition inside `nvme_submit_cmd()`

If the last visible line is the IDENTIFY setup line itself, the most likely
fault is now the first queue-lock acquisition during boot.

### Root Cause 1 — Boot-time sleeplock use before `userinit()`

`nvme_init()` runs from kernel boot in [kernel/core/main.c](/Users/bird/auxv6/kernel/core/main.c) before
`userinit()`.  The NVMe admin queue originally used `struct sleeplock`, and the
first IDENTIFY submission called `acquiresleep(&q->lock)`.

In auxv6, `acquiresleep()` records ownership with `myproc()->pid`.  During this
boot phase there is no current process yet, so dereferencing `myproc()` faults
the kernel exactly at the point where the trace stopped.

**Fix:** NVMe queue locking was switched from `sleeplock` to `spinlock`.
Submission and queue-head/tail updates are short, non-sleeping critical
sections, so `spinlock` is the correct primitive and is safe during boot before
the first process exists.

### Root Cause 1a — Post-enable interrupt masking

The driver also re-applies `INTMS = 0xFFFFFFFF` after `RDY` becomes set during
controller enable.  That remains useful hardening for the polled path, but it
no longer appears to be the primary bootloop trigger.

### Root Cause 1b — PCI capability audit

The PCI-side audit remains useful, but the latest trace does **not** show an
enabled MSI capability:

- Cap ID `0x11` (MSI-X) was present with `mc=0x40`, meaning enable bit 15 was already clear
- No cap ID `0x05` (MSI) was reported in the capability walk

`pci_disable_msi()` is still kept in place as a defensive measure, but it is no
longer the leading explanation for the current bootloop.

### Root Cause 2 — IO queue SQ overflow

`NVME_IO_QUEUE_SIZE` was 256.  Each SQE is 64 bytes, so the SQ alone required
256 × 64 = **16 384 bytes**, but `kalloc()` returns one 4096-byte page.  The
driver wrote submission entries past the end of the page, corrupting adjacent
heap allocations.

**Fix:** `NVME_IO_QUEUE_SIZE` reduced from 256 to 64.  The SQ is now exactly
4096 bytes (one page).  The CQ at 64 × 16 = 1024 bytes fits comfortably.

---

## Implementation Status

| Feature | Status |
|---|---|
| PCI detection + BAR0 map | ✅ complete |
| Controller reset + enable | ✅ complete |
| Admin queue setup | ✅ complete |
| IDENTIFY controller / namespace | ✅ complete |
| I/O queue setup | ✅ complete |
| Read / write (polled) | ✅ complete |
| Block device registration | ✅ complete |
| `nvme_shutdown()` (CC.SHN_NORMAL) | ✅ complete |
| Monotonic CID counter | ✅ complete |
| LBA > BSIZE guard | ✅ complete |
| Recovery memory-leak fix | ✅ complete |
| Spurious IRQ / legacy mask fix | ✅ complete |
| MSI/MSI-X disable at PCI level | ✅ defensive hardening |
| INTMS re-mask after controller enable | ✅ defensive hardening |
| Boot-safe queue locking with spinlock | ✅ complete (current bootloop fix) |
| IO queue size bounded to one page | ✅ complete (bootloop fix) |
| Device number collision fix (loop 40→44) | ✅ complete |
| `ext2`/`ext2fs` fstype alias in sys_mount | ✅ complete |
| End-to-end ext2 mount confirmed | ✅ verified 2026-04-03 |
| MSI-X interrupt mode | ❌ not planned (polled driver) |
| Multiple I/O queues (per-CPU) | ❌ future |
| Namespace management | ❌ future |

---

## Tuning Parameters

| Variable | Default | Effect |
|---|---|---|
| `nvme_cmd_timeout_us` | 1 000 000 | Poll timeout per command (µs) |
| `nvme_rw_retries` | 1 | Retry count on I/O error |

Readable via `/proc/nvme` (if procfs is enabled) or the `nvme_get_tune`
internal API.

## Bring-Up Debugging

Use `DBG_NVME` for deep NVMe-only tracing without turning on global boot spam:

```sh
make EXTRA_CFLAGS="-DDBG_NVME=1" qemu-nvme
# or: make qemu-nvme-dbg
```

Use a single invocation. If you first build with `EXTRA_CFLAGS` and then run a
plain `make qemu-nvme`, the second make updates `.extra_cflags.stamp` to an
empty value and rebuilds the kernel with `DBG_NVME=0`.

Expected early trace sequence:

```text
nvme: DBG_NVME=1 verbose bring-up tracing enabled
pci: 0:2.0 disable_msi start cap=..
pci: 0:2.0 cap@.. id=.. next=..
nvme: init before reset
nvme: reset begin cc=.. csts=..
nvme: reset complete cc=.. csts=..
nvme: init admin queue sq=.. cq=..
nvme: enable begin csts=.. aqa=.. asq=.. acq=..
nvme: enable write cc=..
nvme: enable ready cc=.. csts=..
nvme: init before id_ctrl alloc
nvme: init before identify controller
```

The first missing line after a reboot is the new boundary for the next round of
debugging.

---

## Shutdown

`nvme_shutdown()` is called from `sys_halt` before the port I/O poweroff
sequence.  It sets CC.SHN == 1 (Normal Shutdown) and polls CSTS.SHST until
it reads 2 (Shutdown Complete), with a 5 000 µs timeout per poll iteration
(max 100 iterations).
