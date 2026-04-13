# 6nano(1)

## Name
6nano - Minimal nano/pico-style text editor with a simple control-key workflow.

## Synopsis
```sh
6nano [file]
```

## Duty
Open a text file in a non-modal editor supporting insert/delete/newline editing,
arrow-key navigation, and nano-like save/quit controls.

## Keys
- Arrow keys - move cursor.
- `Enter` - insert newline.
- `Backspace` - delete previous character.
- `Delete` - delete character at cursor.
- `Ctrl+O` - write file.
- `Ctrl+X` - quit (press twice to force quit if unsaved).
- `Ctrl+G` - show key help on status line.

## Notes
- The rootfs staging path installs `/bin/6nano` and creates `/bin/nano`
  and `/bin/pico` symlinks to it.
- If no file is provided, editing starts in an unnamed buffer.
- On first save of an unnamed buffer, 6nano prompts for a file name.

## Examples
```sh
6nano /etc/profile
nano /home/aux/notes.txt
pico /tmp/todo.txt
```

## Source Audit
- Source file: user/6nano.c
- Last updated: 2026-04-12
