# ls(1)

## Name
ls - List directory contents.

## Synopsis
```
ls [-aAldhRrtSd1] [path...]
```

## Duty
List files and directories. For each argument that is a directory, its entries
are listed. For each argument that is a file, the file itself is shown.
Defaults to the current directory if no arguments are given.

## Options
- `-a` — Include all entries, including names beginning with `.`.
- `-A` — Include hidden entries except `.` and `..`.
- `-l` — Long format: mode, links, owner, group, size, mtime (`m/d/y h:m`), name.
- `-h` — Human-readable sizes (`B`, `K`, `M`, `G`); implies long format.
- `-R` — Recurse into subdirectories.
- `-r` — Reverse sort order.
- `-t` — Sort by modification time, newest first.
- `-S` — Sort by size, largest first.
- `-d` — List directory arguments themselves, not their contents.
- `-1` — One entry per line.

## Arguments
- `path...` — Files or directories to list. Defaults to `.` if omitted.

## Default Behavior
- Entries beginning with `.` are hidden unless `-a` or `-A` is specified.
- Names are sorted lexicographically by default.
- `-t` and `-S` select the primary sort key; the later flag wins.
- Without `-l`/`-1`, directory entries are shown in horizontal columns.

## Debugging
- `LS_DEBUG` is a compile-time flag for `ls(1)` internal tracing.
- Build with `make EXTRA_CFLAGS="-DLS_DEBUG=1" _ls` to enable verbose stderr traces.

## Output Format
- Default and `-1`: file name per line.
- `-l`: `mode links owner group size mtime name` (`mtime` is shown as `m/d/y h:m`; mountpoints without a timestamp show `(mountpoint)`)

## Examples
```
ls
ls -A
ls -l /etc
ls -lht
ls -R /usr
ls -d /etc /bin
ls /etc
ls /bin/sh
```

## Source Audit
- Source file: user/ls.c
- Last updated: 2026-04-02
