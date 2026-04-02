# lsblk(1)

## Name
lsblk - List block devices.

## Synopsis
```
lsblk
```

## Duty
Enumerate all block devices (hard disks and partitions) visible to the
kernel. Shows the device name, type, block count, and mount status.

## Options
None.

## Output Columns
- `NAME` — Device name (e.g. `hda`, `hda1`, `vda`)
- `TYPE` — Device type (`disk` or `part`)
- `BLOCKS` — Total number of 512-byte blocks
- `MOUNTPOINT` — Where the device is currently mounted, or blank if not mounted

## Examples
```
lsblk
```

## Source Audit
- Source file: user/lsblk.c
- Last updated: 2026-04-02
