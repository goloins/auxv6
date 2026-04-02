# vblktest(1)

## Name
vblktest - Virtio-blk enumeration and I/O regression test.

## Synopsis
```
vblktest [expected-min-disks]
```

## Duty
Run an in-guest regression pass against detected `/dev/vd*` virtio-blk
disks. Verifies block-device enumeration, `/proc/vblk_flush` visibility,
repeated mount/write/read/umount cycles, and that virtio-blk `ok=`
counters advance.

## Options
None.

## Arguments
- `expected-min-disks` — Minimum number of virtio-blk disks expected.
  Defaults to `2` (matches `make qemu-virtioblktest`). Pass `1` if only
  one virtio-blk disk is present.

## Notes
- Uses `/mnt/vblk0`, `/mnt/vblk1` as mount points.
- Creates temporary files named `io.0` / `io.1` on each mounted volume.
- Each `[PASS]` / `[FAIL]` line identifies the specific check.

## Examples
```
vblktest
vblktest 1
```

## Source Audit
- Source file: user/vblktest.c
- Last updated: 2026-04-02