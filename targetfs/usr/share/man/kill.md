# kill(1)

## Name
kill - Send a signal to processes.

## Synopsis
```
kill [-signo] pid...
```

## Duty
Send a signal to one or more processes by PID using the `sigsend` system call.

## Options
- `-signo` — Signal number to send (e.g. `-9` for SIGKILL, `-15` for SIGTERM).
  Defaults to SIGTERM (15) if not specified.

## Arguments
- `pid...` — One or more process IDs to signal.

## Common Signal Numbers
| Number | Name    | Effect                        |
|--------|---------|-------------------------------|
| 1      | SIGHUP  | Hangup / reload configuration |
| 2      | SIGINT  | Interrupt (Ctrl-C)            |
| 9      | SIGKILL | Force kill (uncatchable)      |
| 15     | SIGTERM | Graceful termination (default)|

## Examples
```
kill 42
kill -9 42
kill -2 100 101 102
```

## Source Audit
- Source file: user/kill.c
- Last updated: 2026-04-02
