# mount(1)

## Name
mount - Mount a filesystem.

## Synopsis
```
mount <dev> <fstype> <path> [opts]
mount <dev> <path> <fstype> [opts]
mount <path> <fstype> [opts]
mount <server>:<export> nfs <path> [opts]
```

## Duty
Mount a filesystem at a specified path. Accepts flexible argument ordering.
Supports block devices, loop devices, `tmpfs`, NFS, and ISO 9660 images.

## Options
None. Mount behavior is controlled through the `opts` argument.

## Arguments
- `dev` — Block device path (e.g. `/dev/hda1`), `tmpfs`, loop device, or
  NFS server path (`host:/export`).
- `fstype` — Filesystem type: `ext2`, `tmpfs`, `nfs`, `isofs`, `proc`, etc.
- `path` — Mount point (must already exist as a directory).
- `opts` — Comma-separated mount options (one or more of):
  - `ro` — Mount read-only
  - `rw` — Mount read-write (default)
  - `nosuid` — Ignore setuid bits
  - `nodev` — Ignore device files
  - `noexec` — Disallow execution of files
  - `sync` — Synchronous writes
  - `remount` — Remount an already-mounted filesystem with new options

## Notes
- Device nodes under `/dev` (e.g. `/dev/cdrom`) are resolved to their
  backing device numbers automatically.
- Argument order is flexible; `mount` parses the combination to determine
  device, fstype, and mountpoint.

## Examples
```
mount /dev/hda1 ext2 /mnt
mount /dev/cdrom isofs /mnt/cdrom
mount tmpfs tmpfs /tmp
mount 10.0.0.1:/export nfs /mnt/nfs
mount /dev/hda1 /mnt ext2 ro
```

## Source Audit
- Source file: user/mount.c
- Last updated: 2026-04-02
