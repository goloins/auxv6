# killall(1)

## Name
killall - Send a signal to processes by name.

## Synopsis
```
killall [-signo] name...
```

## Duty
Send a signal to all running processes whose name matches one of the given
names. Process names are looked up by reading `/proc/ps`.

## Options
- `-signo` — Signal number to send (e.g. `-9` for SIGKILL, `-15` for SIGTERM).
  Defaults to SIGTERM (15) if not specified.

## Arguments
- `name...` — One or more process names to signal (matched against the
  command name, not the full path).

## Examples
```
killall sh
killall -9 zombie
killall -2 ping
```

## Source Audit
- Source file: user/killall.c
- Last updated: 2026-04-02
