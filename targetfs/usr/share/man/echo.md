# echo(1)

## Name
echo - Print arguments to standard output.

## Synopsis
```
echo [-ne] [string...]
```

## Duty
Print each argument separated by a single space.  By default a newline is
appended after the last argument.

## Options
- `-n` — Do not print a trailing newline.
- `-e` — Enable interpretation of backslash escape sequences in each argument.

Flags may be combined (`-ne`, `-en`).  Flag parsing stops at the first
argument that is not a flag.

## Escape Sequences (with -e)
| Sequence | Meaning                          |
|----------|----------------------------------|
| `\\`    | Literal backslash                |
| `\a`    | Alert (bell)                     |
| `\b`    | Backspace                        |
| `\f`    | Form feed                        |
| `\n`    | Newline                          |
| `\r`    | Carriage return                  |
| `\t`    | Horizontal tab                   |
| `\v`    | Vertical tab                     |
| `\0NNN` | Octal value NNN (1–3 digits)    |

## Examples
```
echo hello world
echo -n "no newline"
echo -e "line1\nline2"
echo -e "col1\tcol2"
echo -e "bell\a"
```

## Source Audit
- Source file: user/echo.c
- Last updated: 2026-04-03
