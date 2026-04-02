# sgrep(1)

## Name
sgrep - Search files and input streams using regular expressions (sbase port).

## Synopsis
- sgrep [-EFHchilnqsvwx] [-e pattern] [-f file] [pattern] [file ...]

## Description
- `sgrep` is the upstream sbase `grep` utility built for auxv6.
- It supports BRE/ERE matching, fixed-string mode, pattern files, and stdin/file scanning.

## Options
- `-E` Use extended regular expressions.
- `-F` Use fixed-string matching.
- `-H` Always print file names.
- `-h` Never print file names.
- `-c` Print match count per input.
- `-l` Print only names of files with matches.
- `-n` Prefix matches with line number.
- `-q` Quiet mode; exit on first match.
- `-s` Suppress open/read diagnostics.
- `-v` Invert match result.
- `-w` Match whole words only.
- `-x` Match whole lines only.
- `-e pattern` Add a pattern argument.
- `-f file` Read patterns from file, one per line.

## Examples
- `sgrep -n "root" /etc/passwd`
- `sgrep -F -i "error" /var/log/messages`
- `cat /etc/termcap | sgrep "vt100"`

## Source Audit
- Source file: `ports/sbase/grep.c`
- Build glue: `ports/sbase/Makefile.auxv6`
- Last updated: 2026-04-01
