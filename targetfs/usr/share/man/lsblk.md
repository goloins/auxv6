# lsblk(1)

## Name
lsblk - List block devices.

## Synopsis
```
lsblk [-v]
```

## Duty
Enumerate all block devices (hard disks and partitions) visible to the
kernel. Shows the device name, type, block count, and mount status.

## Options
- `-v` — Verbose mode. Print each device slot probed and the raw block count
  returned by `devblocks(2)` before filtering. Slots returning 0 are shown
  with `[skipped]`. Useful for debugging missing block devices.

## Output Columns
- `NAME` — Device name (e.g. `hda`, `hda1`, `vda`, `nda`)
- `TYPE` — Device type (`disk` or `part`)
- `BLOCKS` — Total number of 512-byte blocks
- `MOUNTPOINT` — Where the device is currently mounted, or blank if not mounted

## Examples
```
lsblk
lsblk -v
```

To cross-reference kernel-side block device registration, check
`/proc/bdev_table` which shows the raw kernel block device table.

## Source Audit
- Source file: user/lsblk.c
- Last updated: 2026-04-03
