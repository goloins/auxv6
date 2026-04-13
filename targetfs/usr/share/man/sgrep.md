# sgrep(1)

## Name
sgrep - Search files using regular expressions (sbase port).

## Synopsis
```
sgrep [-EFHhcilnqsvwx] [-e pattern] [-f file] [pattern] [file...]
```

## Duty
Search files or standard input for lines matching a pattern. This is the
upstream `sbase` `grep` utility built for auxv6. Supports BRE, ERE,
fixed-string mode, and pattern files.

## Options
- `-E` — Use extended regular expressions (ERE) instead of basic (BRE).
- `-F` — Use fixed-string matching; the pattern is not treated as a regex.
- `-H` — Always prefix output with the filename, even for a single file.
- `-h` — Never prefix output with filenames.
- `-c` — Print only a count of matching lines per input file.
- `-i` — Case-insensitive matching.
- `-l` — Print only the names of files that contain at least one match.
- `-n` — Prefix each matching line with its line number.
- `-q` — Quiet mode. Exit immediately on the first match with no output.
- `-s` — Suppress error messages for unreadable files.
- `-v` — Invert match. Print lines that do **not** match the pattern.
- `-w` — Match whole words only (the match must be surrounded by word
  boundaries).
- `-x` — Match whole lines only (the entire line must match the pattern).
- `-e pattern` — Specify a pattern explicitly. May be given multiple times
  to match any of several patterns.
- `-f file` — Read one pattern per line from `file`.

## Arguments
- `pattern` — Regular expression pattern (required unless `-e` or `-f` is used).
- `file...` — Files to search. Reads from stdin if none are given.

## Examples
```
sgrep root /etc/passwd
sgrep -n root /etc/passwd
sgrep -E '^(root|daemon):' /etc/passwd
sgrep -F error /var/log/messages
sgrep -v '^#' /etc/conf
cat /etc/termcap | sgrep vt100
```

## Source Audit
- Source file: ports/sbase/grep.c
- Build glue: ports/sbase/Makefile.auxv6
- Last updated: 2026-04-02
