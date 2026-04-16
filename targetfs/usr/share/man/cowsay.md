# cowsay(1)

## Name
cowsay - render a speech bubble with an ASCII cow.

## Synopsis
```
cowsay [-f cowfile] [-e eyes] [message ...]
```

## Description
`cowsay` prints a speech bubble followed by an ASCII cow template.

By default it loads `default.cow` from:
`/usr/share/games/cows/default.cow`

If no message is provided, the default message is `Moo`.

## Options
```
-f cowfile   Use /usr/share/games/cows/<cowfile>.cow
-e eyes      Set the two-character eye string (default: oo)
-h, --help   Show usage
```

## Files
- `/usr/share/games/cows/default.cow` - default template.
- `/usr/share/games/cows/<name>.cow` - alternate templates.

## Examples
```
cowsay hello world
cowsay -e xx "system online"
cowsay -f default "Moo"
```

## Source Audit
- Source file: user/cowsay.c
- Last updated: 2026-04-13
