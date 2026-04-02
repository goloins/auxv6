# reset(1)

## Name
reset - Reset terminal to a sane state.

## Synopsis
```
reset
```

## Duty
Restore the terminal to a known-good state by applying a standard set of
`termios` flags. Enables echo, canonical (line) mode, and signal processing.
Also flushes the input buffer to clear any pending garbage.

## Options
None.

## Notes
- Useful after a program crashes or corrupts terminal settings.
- Sets: `ECHO`, `ICANON`, `ISIG`, `ICRNL`, `OPOST`, `ONLCR`.
- Clears raw mode and non-blocking flags.
- The terminal is identified by trying stdin, stdout, and stderr in order.

## Examples
```
reset
```

## Source Audit
- Source file: user/reset.c
- Last updated: 2026-04-02
