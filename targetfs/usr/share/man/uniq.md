# uniq(1)

## Name
uniq - Report or filter adjacent repeated lines.

## Synopsis
```
uniq [-cdu] [file]
```

## Description
Reads sorted or otherwise grouped input and collapses adjacent duplicates.

## Options
- `-c` - Prefix lines by repeat count.
- `-d` - Print only duplicated lines.
- `-u` - Print only unique (non-duplicated) lines.

## Notes
- `uniq` only compares adjacent lines. Use `sort` first when needed.

## Source Audit
- Source file: user/uniq.c
- Last updated: 2026-04-06
