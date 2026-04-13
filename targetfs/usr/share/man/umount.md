# umount(1)

## Name
umount - Unmount a filesystem.

## Synopsis
```
umount path
```

## Duty
Unmount the filesystem mounted at `path` using the `umount(2)` system call.

## Options
None.

## Arguments
- `path` — Mount point to unmount. Must be an absolute path to an active
  mount point.

## Notes
- The filesystem must not be busy (no open files or working directories
  on it) for unmounting to succeed.
- Device-based unmounting (by device name) is not supported; always
  specify the mount point.

## Examples
```
umount /mnt
umount /mnt/cdrom
umount /tmp/ramdisk
```

## Source Audit
- Source file: user/umount.c
- Last updated: 2026-04-02
