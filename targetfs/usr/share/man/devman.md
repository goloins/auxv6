# devman(1)

## Name
devman - Scan and print device inventory.

## Synopsis
- 10: * Usage: devman [-s|--scan|-rr]
- usage: %s [-s|--scan|-rr]

## Duty
Scan and print device inventory.

On AHCI systems, ATAPI devices are exposed as `/dev/cdrom`, `/dev/cdrom1`, ...
for ISO media access.

## Options
- `-s` (detected in source usage/option checks)
- `--scan` (detected in source usage/option checks)
- `-rr` (detected in source usage/option checks)

## Examples
- devman -s
- devman -rr

## Source Audit
- Source file: user/devman.c
- Last updated: 2026-04-02
