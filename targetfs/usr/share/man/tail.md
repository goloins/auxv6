# tail(1)

## Name
tail - Print the end of a file, optionally following new content.

## Synopsis
```
tail [-f] file [interval_ticks]
```

## Duty
Display the last 10 lines of `file`. With `-f`, continuously poll the file
for new content and print it as it appears.

## Options
- `-f` — Follow mode. After printing the current tail, keep watching the
  file and print new content as it is appended. Poll interval is
  `interval_ticks` (default: 10 ticks). Press Ctrl-C to stop.

## Arguments
- `file` — Path to the file to read. Required.
- `interval_ticks` — Polling interval in scheduler ticks when using `-f`.
  Defaults to 10. Only meaningful with `-f`.

## Notes
- Always shows the last **10 lines** of the file. There is no `-n` flag
  to change the line count.
- The file is read in full on each poll cycle in follow mode (best for
  small, frequently updated files).
- Buffer size is 4096 bytes; files larger than this are truncated to the
  tail portion.

## Examples
```
tail /var/log/syslog
tail -f /var/log/syslog
tail -f /tmp/out.log 5
```

## Source Audit
- Source file: user/tail.c
- Last updated: 2026-04-02
