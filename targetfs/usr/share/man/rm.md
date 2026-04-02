# rm(1)

## Name
rm - Remove files or directories.

## Synopsis
```
rm [-r] file...
```

## Duty
Delete one or more files or directories. Directories require `-r`.

## Options
- `-r` — Recursive. Remove directories and all of their contents, including
  subdirectories. Required when the target is a directory; `rm` refuses to
  delete a directory without this flag.

## Arguments
- `file...` — One or more files or directories to delete.

## Notes
- Stops at the first error; remaining arguments are not processed.
- The special entries `.` and `..` are never deleted.
- There is no force flag; `rm` prints an error and stops if an operation fails.

## Examples
```
rm file.txt
rm file1 file2 file3
rm -r /tmp/workdir
```

## Source Audit
- Source file: user/rm.c
- Last updated: 2026-04-02
