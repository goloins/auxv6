# 6vi(1)

## Name
6vi - Minimal vi-style text editor with arrow-key navigation.

## Synopsis
```sh
6vi [file]
```

## Duty
Open a text file in a simple modal editor supporting basic insert/delete,
arrow-key movement, and `:` command save/quit flow.

## Modes
- `NORMAL` - movement and commands.
- `INSERT` - text entry.
- `COMMAND` - `:` line command input.

## Keys
- Arrow keys - move cursor.
- `i` - enter insert mode.
- `Esc` - return to normal mode.
- `x` - delete character at cursor (normal mode).
- `Backspace` - delete previous character (insert mode).
- `Enter` - newline split (insert mode).
- `:w` - write file.
- `:q` - quit (fails if unsaved changes exist).
- `:q!` - force quit.
- `:wq` or `:x` - write and quit.

## Notes
- The rootfs staging path creates `/bin/vi` as a symlink to `/bin/6vi`.
- Terminal dimensions are re-read while running so redraw follows current console size.
- Rendering intentionally uses a conservative ANSI subset to stay compatible with
	both UART and framebuffer terminal paths.
- If tty-reported rows/cols are implausibly large, 6vi falls back to a safe
	default grid to avoid drawing text off-screen.
- If no file is provided, editing starts in an unnamed buffer.

## Examples
```sh
6vi /etc/profile
vi /home/aux/notes.txt
```

## Source Audit
- Source file: user/6vi.c
- Last updated: 2026-04-11
