# cp(1)

## Name
cp - Copy files/directories.

## Synopsis
- Usage: cp [-a] [-Rr] [-fiv] [-LPp] source... dest

## Duty
Copy files or directory trees.

## Options
- `-a` preserve mode/ownership, copy directories, and do not dereference symlinks
- `-R` or `-r` recursively copy directories
- `-p` preserve mode/ownership
- `-f` force overwrite
- `-i` prompt before overwrite
- `-v` verbose output
- `-P` do not dereference symlinks
- `-L` follow symlinks (default)

## Notes
- Time stamps are not preserved.

## Examples
- cp file1 file2
- cp -R dir1 dir2
- cp -a srcdir /tmp/backup

## Source Audit
- Source file: user/cp.c
- Last updated: 2026-04-02
