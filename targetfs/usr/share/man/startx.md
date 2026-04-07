# startx

## NAME
startx - convenience launcher for xinit on auxv6

## SYNOPSIS
`startx [xinit-args...]`

## DESCRIPTION
`startx` is a tiny launcher binary that executes:

`/bin/xinit "$@"`

Use it as the user-facing entrypoint for launching x6 sessions.

## EXAMPLES
Launch default session:

```sh
startx
```

Launch a specific client:

```sh
startx /usr/bin/dwm
```
