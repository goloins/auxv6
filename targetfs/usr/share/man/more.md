# more(1)

## Name
more - Terminal pager for text streams.

## Synopsis
```sh
more [-n lines] [file ...]
```

## Duty
Display text one screen at a time, prompting between pages on interactive
terminals.

## Options
- `-n lines` - set page size in lines.

## Notes
- With no files, `more` reads from standard input.
- On a tty, press `space` for next page, `enter` for one line, `q` to quit.
- On non-interactive streams it behaves like a pass-through reader.

## Examples
```sh
more /etc/rc
cat /etc/passwd | more
more -n 40 /var/log/messages
```

## Source Audit
- Source file: user/more.c
- Last updated: 2026-04-06
