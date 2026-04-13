# kill(1)

## Name
kill - Send a signal to processes.

## Synopsis
```
kill [-l]
kill [-s signal] pid...
kill [-signal] pid...
```

## Duty
Send a signal to one or more processes by PID.  The default signal is
SIGTERM (15).  Accepts both numeric signal numbers and symbolic names.

## Options
- `-l` — List all known signal names and their numbers, then exit.
- `-s signal` — Specify the signal by name or number (e.g. `-s TERM`, `-s 9`,
  `-s SIGKILL`).
- `-signal` — Shorthand: specify signal as an argument prefix (e.g. `-9`,
  `-TERM`, `-SIGTERM`, `-HUP`).

## Signal Names
Signal names may be given with or without the `SIG` prefix:
`TERM`, `SIGTERM`, and `15` are all equivalent.

| Number | Name    | Effect                          |
|--------|---------|----------------------------------|
| 1      | HUP     | Hangup / reload configuration   |
| 2      | INT     | Interrupt (Ctrl-C)              |
| 3      | QUIT    | Quit with core dump             |
| 9      | KILL    | Force kill (uncatchable)        |
| 10     | USR1    | User-defined signal 1           |
| 12     | USR2    | User-defined signal 2           |
| 15     | TERM    | Graceful termination (default)  |
| 17     | CHLD    | Child status changed            |
| 18     | CONT    | Continue if stopped             |
| 19     | STOP    | Stop (uncatchable)              |
| 20     | TSTP    | Terminal stop (Ctrl-Z)          |

## Examples
```
kill 42
kill -9 42
kill -TERM 100 101 102
kill -SIGKILL 7
kill -s HUP 1234
kill -l
```

## Source Audit
- Source file: user/kill.c
- Last updated: 2026-04-03
