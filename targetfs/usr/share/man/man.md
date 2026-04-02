# man(1)

## Name
man - Display manual pages.

## Synopsis
```
man [-l] topic
```

## Duty
Locate and render the manual page for `topic` from `/usr/share/man/<topic>.md`.
Markdown formatting (headings, lists, code fences) is rendered for readability.

## Options
- `-l` — Enable paging. Output is paused every 22 lines with a `--More--`
  prompt. Press **Enter** to advance one page or **q** to quit.

## Arguments
- `topic` — Name of the command or topic to look up. Resolved as
  `/usr/share/man/<topic>.md`.

## Notes
- Man pages are formatted in Markdown. Headings, bullet lists, and code
  fences receive visual treatment when displayed on a terminal.
- Without `-l`, the full page is printed without pausing.

## Examples
```
man ls
man -l grep
man mount
```

## Source Audit
- Source file: user/man.c
- Last updated: 2026-04-02
- Source file: user/man.c
- Last updated: 2026-04-01
