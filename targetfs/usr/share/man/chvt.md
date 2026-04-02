# chvt(1)

## Name
chvt - Switch the active virtual terminal.

## Synopsis
```
chvt [tty_number]
```

## Duty
With no arguments, display the currently active TTY number. With an argument,
switch the active console to the specified virtual terminal.

## Options
None.

## Arguments
- `tty_number` — Virtual terminal number to switch to (e.g. `1`, `2`, `3`).
  If omitted, prints the current active TTY.

## Notes
- Uses `ioctl(TIOCSACTIVETTY)` / `ioctl(TIOCGACTIVETTY)` to query or set the
  active terminal.
- Requires access to a controlling TTY (opened from stdin, stdout, or stderr).

## Examples
```
chvt          # print current TTY
chvt 2        # switch to TTY 2
chvt 1        # switch back to TTY 1
```

## Source Audit
- Source file: user/chvt.c
- Last updated: 2026-04-02
