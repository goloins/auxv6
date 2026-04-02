# mounttest(1)

## Name
mounttest - Mount/unmount regression test.

## Synopsis
```
mounttest
```

## Duty
Run a basic regression pass against the mount subsystem. Tests procfs and
tmpfs mounting, creates and reads files through the mounted filesystems,
then unmounts and verifies cleanup.

## Options
None.

## Tests Performed
1. **procfs mount** — Mounts `/proc` and verifies it is accessible.
2. **tmpfs mount** — Creates a tmpfs at `/mnt/tmp` and writes/reads a file.
3. **Unmount** — Unmounts and confirms the mount point is empty.

## Examples
```
mounttest
```

## Source Audit
- Source file: user/mounttest.c
- Last updated: 2026-04-02
