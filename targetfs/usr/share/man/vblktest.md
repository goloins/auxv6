# vblktest(1)

## Name
vblktest - exercise virtio-blk enumeration, mount cycles, and basic I/O.

## Synopsis
- vblktest [expected-min-disks]

## Duty
Run an in-guest regression pass against detected `/dev/vd*` disks. The suite verifies block-device enumeration, `/proc/vblk_flush` visibility, repeated mount/write/read/umount cycles, and that virtio-blk `ok=` counters advance.

## Notes
- Default expectation is 2 virtio-blk disks, which matches `make qemu-virtioblktest`.
- Pass `1` if running under a configuration with only one virtio-blk disk.
- The test uses `/mnt/vblk0`, `/mnt/vblk1`, and temporary files named `io.0` / `io.1` on each mounted volume.

## Examples
- vblktest
- vblktest 1

## Source Audit
- Source file: user/vblktest.c
- Last updated: 2026-04-01