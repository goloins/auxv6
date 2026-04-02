# isotest(1)

## Name
isotest - ISO 9660 filesystem and loop device regression test.

## Synopsis
```
isotest
```

## Duty
Run a regression test suite against the ISO 9660 (isofs) filesystem driver
and loop device subsystem. Tests loop device setup, ISO image mounting,
directory operations, and file I/O through the mounted image.

## Options
None.

## Tests Performed
1. **Loop device setup** — Attaches an ISO image via `/dev/loop0`.
2. **Mount** — Mounts the loop device as an ISO 9660 filesystem.
3. **Directory listing** — Reads and verifies directory entries.
4. **File I/O** — Opens and reads files from the ISO image.
5. **Unmount and detach** — Cleans up loop device and mount point.

## Examples
```
isotest
```

## Source Audit
- Source file: user/isotest.c
- Last updated: 2026-04-02

## Duty
Run a regression test suite against the ISO 9660 (isofs) filesystem driver
and loop device subsystem. Tests loop device setup, ISO image mounting,
directory operations, and file I/O through the mounted image.

## Options
None.

## Tests Performed
1. **Loop device setup** — Attaches an ISO image via `/dev/loop0`.
2. **Mount** — Mounts the loop device as an ISO 9660 filesystem.
3. **Directory listing** — Reads and verifies directory entries.
4. **File I/O** — Opens and reads files from the ISO image.
5. **Unmount and detach** — Cleans up loop device and mount point.

## Examples
```
isotestit
```

## Source Audit
- Source file: user/isotest.c
- Last updated: 2026-04-02
