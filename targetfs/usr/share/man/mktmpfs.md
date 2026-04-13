# mktmpfs(1)

## Name
mktmpfs - Create a tmpfs mount with a size limit.

## Synopsis
```
mktmpfs <mountpoint> <size>
```

## Duty
Create a `tmpfs` filesystem and mount it at `mountpoint` with a fixed
maximum size. The filesystem lives entirely in RAM.

## Options
None.

## Arguments
- `mountpoint` — Directory at which to mount the new tmpfs. Must already
  exist.
- `size` — Maximum size of the filesystem. Accepts an optional suffix:
  - `k` or `K` — Kilobytes (× 1024)
  - `m` or `M` — Megabytes (× 1024²)
  - `g` or `G` — Gigabytes (× 1024³)
  - No suffix — Bytes

## Notes
- The mountpoint must already exist and be a directory.
- Uses `mount(2)` with filesystem type `tmpfs` and the size as the
  `size=` option string.

## Examples
```
mktmpfs /tmp 32M
mktmpfs /mnt/ramdisk 64M
mktmpfs /var/shm 1073741824
```

## Source Audit
- Source file: user/mktmpfs.c
- Last updated: 2026-04-02
