# telinit(1)

## Name
telinit - Request a system runlevel transition.

## Synopsis
```
telinit <0|1|2|3|4|5|6|S>
```

## Duty
Write the requested runlevel to `/etc/.runlevel.req` and send SIGHUP to
`init` (PID 1), triggering a runlevel change.

## Options
None.

## Arguments
- `runlevel` — The target runlevel. One of:
  - `0` — Halt the system
  - `1` — Single-user mode
  - `2`–5 — Multi-user modes
  - `6` — Reboot
  - `S` or `s` — Single-user rescue mode (normalised to `S`)

## Notes
- Only one argument is accepted. Extra characters in the argument are rejected.
- Requires write access to `/etc/.runlevel.req` (typically root).

## Examples
```
telinit 3
telinit 6
telinit S
telinit 0
```

## Source Audit
- Source file: user/telinit.c
- Last updated: 2026-04-02
