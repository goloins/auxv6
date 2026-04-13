# devman(1)

## Name
devman - Device node manager.

## Synopsis
```
devman [-s|--scan]
devman [-rr]
```

## Duty
Scan the kernel device inventory and create device nodes under `/dev`. Reads
configuration from `/etc/devman.conf`. With no arguments, prints the current
device inventory without creating nodes.

## Options
- `-s`, `--scan` — Scan system devices and create all missing device nodes
  under `/dev`. This is the normal operational mode.
- `-rr` — Rescan and **replace** all managed device nodes (removes and
  recreates existing `/dev` entries).

## Notes
- On AHCI systems, ATAPI (CD-ROM) devices are exposed as `/dev/cdrom`,
  `/dev/cdrom1`, etc.
- Device node types and minor numbers are sourced from `/proc/devices`.
- The `debug=` key in `/etc/devman.conf` enables verbose output.
- `devman -s` is typically invoked from `/etc/rc.local` at boot.

## Examples
```
devman -s
devman -rr
devman         # list inventory without creating nodes
```

## Source Audit
- Source file: user/devman.c
- Last updated: 2026-04-02
