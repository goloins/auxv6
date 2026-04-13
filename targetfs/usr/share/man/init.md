# init(1)

## Name
init - System initialization process (PID 1).

## Synopsis
```
init
```

## Duty
The primary init process. Spawns login shells on available terminals, runs
`/etc/rc.local` at boot, and handles runlevel transitions signaled via SIGHUP.
Manages the `/etc/runlevel` state file. Should always be PID 1.

## Options
None.

## Notes
- Runlevel changes are requested by writing to `/etc/.runlevel.req` and
  sending SIGHUP to PID 1 (use `telinit` for this).
- `init` is started by the kernel and must not be invoked directly.
- If a child process exits unexpectedly, init will respawn it.

## Examples
```
# Normally started by the kernel at boot; not invoked manually.
telinit 6   # request a reboot via init
```

## Source Audit
- Source file: user/init.c
- Last updated: 2026-04-02
