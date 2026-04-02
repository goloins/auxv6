# grep(1)

## Name
grep - Search for lines matching a pattern.

## Synopsis
```
grep pattern [file...]
```

## Duty
Print each line from the given files (or standard input) that matches
`pattern`. Uses a simple regular expression engine supporting the operators
below.

## Options
None. Flags are not supported in this implementation.

## Arguments
- `pattern` — Regular expression to match. Supports:
  - `^` — Anchor match to start of line
  - `$` — Anchor match to end of line
  - `.` — Match any single character
  - `*` — Match zero or more of the preceding character or `.`
- `file...` — Files to search. If omitted, reads from standard input.

## Notes
- Matching is not case-insensitive; use `.` wildcards if needed.
- Only basic regex operators (`^ . * $`) are supported (K&R algorithm).
- Reads up to 1024 bytes at a time; very long lines may be truncated.

## Examples
```
grep root /etc/passwd
grep ^error /var/log/syslog
cat /etc/passwd | grep daemon
```

## Source Audit
- Source file: user/grep.c
- Last updated: 2026-04-02
