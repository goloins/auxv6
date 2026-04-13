# losetup(1)

## Name
losetup - Configure loop block devices.

## Synopsis
```
losetup                           # list all loop devices
losetup /dev/loopN /path/to/file  # attach file to loop device
losetup -d /dev/loopN             # detach loop device
losetup -f /path/to/file          # attach to first free loop device
```

## Duty
Attach regular files as loop block devices, detach them, or list the
current status of all loop devices.

## Options
- `-d` — Detach the specified loop device, releasing the backing file.
- `-f` — Find and use the first available (free) loop device for the
  given file.

## Arguments
- `/dev/loopN` — Loop device node (e.g. `/dev/loop0`, `/dev/loop1`).
- `/path/to/file` — Regular file to use as the loop device backing store.
  Must be a regular file or a filesystem image.

## Notes
- With no arguments, prints the status of all existing loop devices.
- Loop devices are numbered starting from 0 (`/dev/loop0`).
- Files attached via losetup can be mounted like block devices.

## Examples
```
losetup                              # list loop devices
losetup /dev/loop0 /tmp/disk.img     # attach image
losetup -f /tmp/disk.img             # attach to first free
losetup -d /dev/loop0                # detach
```

## Source Audit
- Source file: user/losetup.c
- Last updated: 2026-04-02
