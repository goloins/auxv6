# xinit

## NAME
xinit - launch an x6 session and run a client program

## SYNOPSIS
`xinit [client [args...]] [-- x6-args...]`

## DESCRIPTION
`xinit` is a small session launcher for phase-1 x6 bring-up.

Behavior:

1. Start `/bin/x6 -f` with optional x6 args.
2. Probe readiness using `HELLO x6/1` on loopback.
3. Launch the requested client command.
4. When the client exits, terminate x6 and wait for clean shutdown.

If no client is provided, `xinit` checks rc scripts in this order: `$HOME/.xinitrc`, then `/.xinitrc`, then `/etc/xinitrc`. If found, it runs `/bin/dash <xinitrc>`. Otherwise it runs `/bin/dash`.

## ARGUMENTS
- `client [args...]` command to run once x6 is ready
- `-- x6-args...` arguments passed directly to x6 (for example `-p 6006`)

## EXIT STATUS
- returns the client exit status when possible
- non-zero on launcher failures

## EXAMPLES
Start a shell session under x6:

```sh
xinit
```

Start dwm when available:

```sh
xinit /bin/dwm
```

Use a custom x6 port:

```sh
xinit /bin/sh -- -p 6010
```
