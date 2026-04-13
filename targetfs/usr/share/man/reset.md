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
Also flushes pending terminal I/O to clear any queued garbage.

## Options
None.

## Notes
- Useful after a program crashes or corrupts terminal settings.
- Sets: `ECHO`, `ICANON`, `ISIG`, `ICRNL`, `OPOST`, `ONLCR`.
- The terminal is identified by trying stdin, stdout, and stderr in order.
- Preserves the terminal's existing winsize instead of forcing 80x24.

## Examples
```
reset
```

## Source Audit
- Source file: user/reset.c
- Last updated: 2026-04-02
