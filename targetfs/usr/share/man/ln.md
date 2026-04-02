# ln(1)

## Name
ln - Create links between files.

## Synopsis
```
ln [-s] old new
```

## Duty
Create a link named `new` pointing to `old`. By default creates a hard link;
with `-s`, creates a symbolic link.

## Options
- `-s` — Create a **symbolic link** (symlink) instead of a hard link.
  Symlinks store the path string of the target, can cross filesystem
  boundaries, and can point to directories.

## Arguments
- `old` — Target path (the file or directory being pointed to).
- `new` — Name of the link to create.

## Notes
- Hard links cannot cross filesystem boundaries and cannot link directories.
- Symbolic links can point to non-existent targets (broken symlinks).

## Examples
```
ln /bin/sh /bin/dash      # hard link
ln -s /usr/bin/vi /bin/vi # symlink
ln -s ../lib/foo.so foo.so
```

## Source Audit
- Source file: user/ln.c
- Last updated: 2026-04-02
