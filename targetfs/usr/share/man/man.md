# man(1)

## Name
man - Display manual pages.

## Synopsis
```
man -l
man topic
```

## Duty
Locate and render the manual page for `topic` from `/usr/share/man/<topic>.md`.
Markdown formatting (headings, lists, code fences) is rendered for readability.

## Options
- `-l` — List available topics under `/usr/share/man`.

## Arguments
- `topic` — Name of the command or topic to look up. Resolved as
  `/usr/share/man/<topic>.md`.

## Notes
- Man pages are formatted in Markdown. Headings, bullet lists, and code
  fences receive visual treatment when displayed on a terminal.
- Inline formatting is also rendered for common constructs: `code`,
  `**bold**`, `*emphasis*`, ordered lists, block quotes (`> ...`),
  horizontal rules, and Markdown links (`[text](url)`).
- When standard input and output are terminals, output is paged every
  24 lines with a `--More--` prompt.
- At the pager prompt, press **Enter** to advance one page or press
  **q** to quit immediately.

## Examples
```
man ls
man grep
man -l
man mount
```

## Source Audit
- Source file: user/man.c
- Last updated: 2026-04-02
