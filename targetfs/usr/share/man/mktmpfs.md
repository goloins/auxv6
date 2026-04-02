# mktmpfs(1)

## Name
mktmpfs - create a tmpfs mount with a size limit

## Synopsis
- mktmpfs <mountpoint> <size>

## Duty
Create a tmpfs filesystem mounted at the target directory with a fixed size limit.

## Options
- none

## Examples
- mktmpfs /tmp 32M
- mktmpfs /mnt/ramdisk 1048576

## Notes
- Size accepts optional K, M, or G suffixes (powers of 1024).
- The mountpoint must already exist and be a directory.

## Source Audit
- Source file: user/mktmpfs.c
- Last updated: 2026-04-02
