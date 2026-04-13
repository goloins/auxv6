# ps(1)

## Name
ps - List running processes.

## Synopsis
```
ps
```

## Duty
List all processes currently running on the system by reading the
`/proc/ps` kernel interface.

## Options
None.

## Output Columns
- `PID` — Process ID
- `PPID` — Parent process ID
- `STATE` — Process state (running, sleeping, zombie, etc.)
- `NAME` — Command name

## Examples
```
ps
ps | grep sh
```

## Source Audit
- Source file: user/ps.c
- Last updated: 2026-04-02
