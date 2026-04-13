# termdemo(1)

## Name
termdemo - Terminal feature demonstration.

## Synopsis
```
termdemo
```

## Duty
Demonstrate terminal capabilities by exercising ANSI escape sequences and
other terminal features. Useful for verifying that the terminal emulator
and VT driver are functioning correctly.

## Options
None.

## Features Demonstrated
- **ANSI colors** — Prints text in all 8 foreground and background colors.
- **UTF-8 drawing characters** — Box-drawing and other Unicode symbols.
- **Insert mode** — Tests character insertion with `\033[4h` / `\033[4l`.
- **Line wrap modes** — Tests wrapping at terminal width boundary.
- **Erase sequences** — Line erase (`\033[K`) and screen erase (`\033[J`).
- **Alternate screen buffer** — Switches to and from the alt screen.

## Examples
```
termdemo
```

## Source Audit
- Source file: user/termdemo.c
- Last updated: 2026-04-02
