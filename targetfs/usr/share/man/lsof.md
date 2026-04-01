# lsof(1)

## Name
lsof - List open files from /proc/lsof.

## Synopsis
- usage: lsof [pid]

## Duty
Show open file descriptors for all processes, or for a specific PID.

## Options
- `pid` (optional numeric process ID filter)

## Examples
- lsof
- lsof 1

## Notes
- Reads from `/proc/lsof`.
- Columns are: PID FD TYPE RW DEV INO OFF NAME.

## Source Audit
- Source file: user/lsof.c
- Last updated: 2026-04-01
