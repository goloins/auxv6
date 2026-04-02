# cp(1)

## Name
cp - Copy files and directories.

## Synopsis
```
cp [-a] [-Rr] [-fivLPp] source... dest
```

## Duty
Copy files or directory trees from one location to another.

## Options
- `-a` — Archive mode. Equivalent to `-Rr -p`. Preserves mode/ownership,
  copies directories recursively, and does not dereference symlinks.
- `-R`, `-r` — Recursive. Required when copying directories; copies the
  entire directory tree.
- `-f` — Force. Overwrite the destination without prompting.
- `-i` — Interactive. Prompt before overwriting an existing destination
  file.
- `-v` — Verbose. Print each file name as it is copied.
- `-L` — Follow symlinks. Dereference symbolic links in the source
  (this is the default behavior).
- `-P` — Do not follow symlinks. Copy the symlink itself rather than
  the file it points to.
- `-p` — Preserve. Retain the source file's mode and ownership in the
  destination copy.

## Arguments
- `source...` — One or more source files or directories.
- `dest` — Destination file path or directory.

## Notes
- Timestamps are not preserved.
- If multiple sources are given, `dest` must be a directory.
- Device file copying preserves the device number.

## Examples
```
cp file1 file2
cp -R dir1 dir2
cp -a srcdir /tmp/backup
cp -ipv *.txt /mnt/usb/
```

## Source Audit
- Source file: user/cp.c
- Last updated: 2026-04-02
