# rm(1)

## Name
rm - Remove files or directories.

## Synopsis
```
rm [-rRfiv] file...
```

## Duty
Delete one or more files or directories.  Directories require `-r` / `-R`.
Continues processing remaining arguments after an error (unlike the previous
stop-on-first-error behaviour).

## Options
- `-r`, `-R` — Recursive. Remove directories and all of their contents.
- `-f` — Force. Ignore nonexistent files, never prompt, and always exit 0.
  Overrides `-i`.
- `-i` — Interactive. Prompt before removing each file or descending into each
  directory. Overrides `-f`.
- `-v` — Verbose. Print `removed 'path'` for each file actually deleted.

## Arguments
- `file...` — One or more files or directories to delete.

## Notes
- `.` and `..` are unconditionally refused.
- Uses `lstat(2)` so symlinks themselves are removed, not their targets.
- Uses `opendir`/`readdir` internally for portable directory traversal.

## Examples
```
rm file.txt
rm -f nonexistent_file
rm -rv /tmp/workdir
rm -i sensitive.txt
rm file1 file2 file3
```

## Source Audit
- Source file: user/rm.c
- Last updated: 2026-04-03
