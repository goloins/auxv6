# file(1)

## Name
file - Identify file type by magic/signature checks.

## Synopsis
- usage: file path...

## Duty
Classify files by header signatures and simple heuristics.

## Options
- none detected

## Examples
- file /bin/sh
- file README
- file /tmp/test.iso

## Notes
- Recognizes common formats including ELF, scripts, archives, compressed data, images, audio, SQLite, ext filesystem images, ISO-9660, text, and generic data.
- This implementation is built-in and does not depend on a magic database.

## Source Audit
- Source file: user/file.c
- Last updated: 2026-04-01
