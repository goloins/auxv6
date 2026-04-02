# mount(1)

## Name
mount - Mount filesystems.

## Synopsis
- usage: mount [fstab]|<path> <fstype> [flags]|<dev> <fstype> <path> [flags]

## Duty
Mount filesystems.

Device nodes under `/dev` (for example `/dev/cdrom`) are accepted as the
`<dev>` argument and resolve to their backing device number.

## Options
- none detected

## Examples
- mount /dev/cdrom isofs /mnt/cdrom
- mount /dev/hda /mnt ext2

## Source Audit
- Source file: user/mount.c
- Last updated: 2026-04-02
