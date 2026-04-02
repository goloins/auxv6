# mv(1)

## Name
mv - Move or rename a file or directory.

## Synopsis
```
mv old new
```

## Duty
Rename `old` to `new` using the `rename(2)` system call. Works for both
files and directories. If `new` already exists it is replaced.

## Options
None.

## Arguments
- `old` — Existing file or directory path.
- `new` — Target name or path.

## Notes
- Both arguments are required; exactly two operands must be given.
- Cross-filesystem moves (between different mount points) are not supported
  by the underlying `rename(2)` implementation.

## Examples
```
mv old.txt new.txt
mv /tmp/draft /home/user/document
mv srcdir destdir
```

## Source Audit
- Source file: user/mv.c
- Last updated: 2026-04-02
