# startx

## NAME
startx - convenience wrapper for xinit on auxv6

## SYNOPSIS
`startx [xinit-args...]`

## DESCRIPTION
`startx` is a thin wrapper that executes:

`/bin/xinit "$@"`

Use it as the user-facing entrypoint for launching x6 sessions.

## EXAMPLES
Launch default session:

```sh
startx
```

Launch a specific client:

```sh
startx /bin/dwm
```
