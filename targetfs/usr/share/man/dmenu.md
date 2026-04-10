# dmenu

## Synopsis

`dmenu [-bfiv] [-l lines] [-m monitor] [-p prompt] [-fn font] [-nb color] [-nf color] [-sb color] [-sf color]`

`dmenu_run [dmenu options...]`

## Description

`dmenu` reads newline-delimited items from standard input, presents them in a small X11 menu, and writes the selected entry to standard output.

The auxv6 build is a first-class integration of the vendored `ports/dmenu-5.4` sources. It links against the auxv6 X11/Xft/Xinerama shim layer from `user/x11.o` and uses the repo's bundled X11 headers under `include/X11/`.

`dmenu_run` is installed as a shell helper that feeds the current executable set into `dmenu`. It uses `dmenu_path` to cache and refresh the command list and relies on `stest` to scan `$PATH`.

## Options

- `-b` place the menu at the bottom of the screen instead of the top.
- `-f` grab the keyboard before reading from standard input.
- `-i` match items case-insensitively.
- `-v` print the version and exit.
- `-l lines` show a vertical list with the given number of lines.
- `-m monitor` display the menu on the selected monitor number.
- `-p prompt` show `prompt` to the left of the input field.
- `-fn font` use `font` for the menu font set.
- `-nb color` use `color` for the normal background.
- `-nf color` use `color` for the normal foreground.
- `-sb color` use `color` for the selected background.
- `-sf color` use `color` for the selected foreground.

## Files

- `/usr/bin/dmenu` main X11 chooser.
- `/usr/bin/dmenu_run` command launcher helper used by `dwm`.
- `/usr/bin/dmenu_path` cached `$PATH` enumerator.
- `/usr/bin/stest` helper used by `dmenu_path`.

## Notes

- `dwm` in this tree is already configured to launch `/usr/bin/dmenu_run`.
- Monitor selection uses the auxv6 Xinerama shim when multiple screens are exposed.