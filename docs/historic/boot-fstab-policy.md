# Boot Fstab Policy

Updated: 2026-04-03

## Scope

This note documents the intended ownership and contents of the boot-time
`/etc/fstab` used by the ext2-root development flow.

## Policy

- `targetfs/etc/rc.S` is the single owner of the boot-time `/etc/fstab` mount
  pass.
- The ext2 root filesystem is mounted by the kernel at `/`; it must not be
  mounted again from `/etc/fstab` on `/mnt` or any other path.
- `/mnt` is a plain directory on the root filesystem reserved for explicit
  runtime mounts such as removable media, test images, or network filesystems.
- The shipped boot-time `fstab` should contain only auxiliary mounts that are
  required during bootstrap, such as `/proc`.

## Current ext2-root behavior

- `targetfs/etc/fstab.ext2root` now mounts `/proc` only.
- `targetfs/etc/fstab` follows the same rule so ad hoc rootfs builds do not
  reintroduce a duplicate `/mnt` entry.