# ls(1)

## Name
ls - List directory contents.

## Synopsis
```
ls [path...]
```

## Duty
List files and directories. For each argument that is a directory, its entries
are listed. For each argument that is a file, the file itself is shown.
Defaults to the current directory if no arguments are given.

## Options
None. Flags are not supported in this implementation.

## Arguments
- `path...` — Files or directories to list. Defaults to `.` if omitted.

## Output Format
Each entry is printed as:
```
name  type  owner  group  size  permissions
```
- `type` — `1` for regular file, `2` for directory
- `size` — human-readable with `B`/`K`/`M`/`G` suffix
- `permissions` — four octal digits: sticky/setuid/setgid, owner, group, other

## Examples
```
ls
ls /etc
ls /bin/sh
```

## Source Audit
- Source file: user/ls.c
- Last updated: 2026-04-02
