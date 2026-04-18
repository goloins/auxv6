# 6write

## Name
6write - lightweight pure-X11 text editor for auxv6 X stack bring-up

## Synopsis
6write [file]

## Description
6write is a small editor implemented directly on top of Xlib calls.

It is intended as a compatibility probe for the auxv6 X11 stack and window manager behavior, with no toolkit dependency and no AUX menu protocol integration.

The application owns and draws its own in-window menu bar and dropdown menus.

## Menus
File:
- New
- Save
- Quit

Edit:
- Select All (visual highlight mode)

## Keyboard
- Ctrl+S: save
- Ctrl+Q: quit (requires second invoke when buffer is dirty)
- Ctrl+N: new buffer
- Ctrl+A: select all (visual)
- Arrow keys: move cursor
- Home/End: line start/end
- PageUp/PageDown: coarse vertical movement
- Enter: insert newline
- Backspace/Delete: delete text

## Notes
- If no path is provided, Save writes to `6write.txt` in the current directory.
- If a file path is provided and open fails, 6write starts with an empty buffer and keeps the path in the title.

## Purpose In auxv6
6write is a first-party raw-X11 editor used to expose missing or incorrect Xlib/WM behavior without hiding issues behind external toolkit abstractions.
