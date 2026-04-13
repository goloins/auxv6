# grep(1)

## Name
grep - Search for lines matching a pattern.

## Synopsis
```
grep [-EHhinrvc] [-l] pattern [file...]
```

## Duty
Print each line from the given files (or standard input) that matches
`pattern`. Uses the full POSIX regex engine (BRE by default, ERE with `-E`)
supporting character classes, alternation, quantifiers, and anchors.

Exits 0 if any match was found, 1 if no match, 2 on error.

## Options
- `-E` — Use Extended Regular Expressions (ERE).
- `-i` — Case-insensitive matching.
- `-v` — Invert match: print lines that do **not** match.
- `-n` — Prefix each matching line with its 1-based line number.
- `-c` — Print only a count of matching lines per file.
- `-l` — Print only the names of files that contain at least one match.
- `-h` — Suppress the filename prefix on output (opposite of `-H`).
- `-H` — Always print the filename prefix on each match line.
- `-r` — Recurse into directories.

## Arguments
- `pattern` — Regular expression to match.
- `file...` — Files to search. If omitted, reads from standard input.

## Regex Syntax (BRE default)
- `^` — Anchor to start of line
- `$` — Anchor to end of line
- `.` — Match any single character
- `*` — Zero or more of the preceding element
- `[abc]` / `[^abc]` — Character class / negated class
- `\+` / `\?` — One-or-more / zero-or-one (BRE escapes)
- Add `-E` for ERE where `+`, `?`, `|`, `(`, `)` are unescaped.

## Notes
- Lines longer than 4095 characters are split at that boundary.
- When searching multiple files the filename is prefixed unless `-h` is given.

## Examples
```
grep root /etc/passwd
grep -i error /var/log/syslog
grep -n 'foo\|bar' file.txt
grep -E 'foo|bar' file.txt
grep -c daemon /etc/passwd
grep -r TODO /usr/src
cat /etc/passwd | grep -v root
```

## Source Audit
- Source file: user/grep.c
- Last updated: 2026-04-03
