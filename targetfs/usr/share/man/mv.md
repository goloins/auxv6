# mv(1)

## Name
mv - Move or rename files and directories.

## Synopsis
```
mv [-fiv] source... dest
```

## Duty
Move one or more source files or directories to `dest`.  When `dest` is an
existing directory, each source is moved into it under its original basename.
Otherwise exactly one source must be given and it is renamed to `dest`.

Uses `rename(2)` internally.  If the source and destination are on different
filesystems (`EXDEV`) the file is copied byte-for-byte then the source is
removed.

## Options
- `-f` — Force: never prompt before overwriting an existing destination.
- `-i` — Interactive: prompt (`y/n`) before overwriting an existing destination.
- `-v` — Verbose: print `'src' -> 'dest'` for each moved file.

If both `-f` and `-i` are given the last one on the command line wins.

## Arguments
- `source...` — One or more source paths.
- `dest` — Destination path or directory.

## Notes
- When multiple sources are given `dest` must be a directory.
- Permissions are preserved on cross-device copies.

## Examples
```
mv old.txt new.txt
mv -v /tmp/draft /home/user/document
mv -i file1 file2 /tmp/dest/
mv file1 file2 file3 destdir/
```

## Source Audit
- Source file: user/mv.c
- Last updated: 2026-04-03
