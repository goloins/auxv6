# clear(1)

## Name
clear - Clear the terminal screen.

## Synopsis
```
clear
```

## Duty
Erases the visible content of the terminal and moves the cursor to the
top-left corner by writing ANSI escape sequences to the console device.

## Options
None.

## Notes
- Sends `\033[0m\033[2J\033[H` to the first open TTY found among stdin,
  stdout, and stderr.
- Does not restore terminal state or scroll history.

## Examples
```
clear
```

## Source Audit
- Source file: user/clear.c
- Last updated: 2026-04-02
