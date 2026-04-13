# cat(1)

## Name
cat - Concatenate files to standard output.

## Synopsis
```
cat [file...]
```

## Duty
Read one or more files in sequence and write their contents to standard output.
If no files are given, reads from standard input.

## Options
None. `cat` is argument-only; flags are not supported.

## Arguments
- `file...` — One or more files to concatenate. Read and output in order.
  If omitted, standard input is used.

## Notes
- When printing multiple files, a trailing newline is appended if the file's
  last byte is not already a newline.
- Exit status is non-zero on read or write errors.

## Examples
```
cat /etc/passwd
cat file1.txt file2.txt
cat < input.txt
```

## Source Audit
- Source file: user/cat.c
- Last updated: 2026-04-02
