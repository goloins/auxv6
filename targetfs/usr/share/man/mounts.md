# mounts(1)

## Name
mounts - List mounted filesystems.

## Synopsis
```
mounts
```

## Duty
Query the kernel and print the current mount table, showing each active
mount with its device, flags, filesystem type, and mount point.

## Options
None.

## Output Columns
- `dev` — Device major:minor or name
- `flags` — Mount flags (e.g. `RO`, `NOSUID`)
- `fstype` — Filesystem type (e.g. `ext2`, `tmpfs`, `proc`)
- `path` — Mount point in the filesystem namespace

## Examples
```
mounts
```

## Source Audit
- Source file: user/mounts.c
- Last updated: 2026-04-02
