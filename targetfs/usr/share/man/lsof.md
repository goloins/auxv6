# lsof(1)

## Name
lsof - List open file descriptors.

## Synopsis
```
lsof [pid]
```

## Duty
Show all open file descriptors across all processes, or only for one
specific process, by reading from `/proc/lsof`.

## Options
None.

## Arguments
- `pid` — Optional numeric process ID. If given, only file descriptors
  belonging to that process are shown.

## Output Columns
```
PID  FD  TYPE  RW  DEV  INO  OFF  NAME
```
- `PID` — Process ID
- `FD` — File descriptor number
- `TYPE` — File type (regular, directory, device, socket, pipe)
- `RW` — Access mode (r, w, rw)
- `DEV` — Device major:minor
- `INO` — Inode number
- `OFF` — Current file offset
- `NAME` — File path or description

## Examples
```
lsof
lsof 1
lsof 42
```

## Source Audit
- Source file: user/lsof.c
- Last updated: 2026-04-02
