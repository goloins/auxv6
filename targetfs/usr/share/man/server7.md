# server7

## NAME
server7 - bootstrap display server daemon endpoint

## SYNOPSIS
`server7 [-f] [-p port] [-m desktop|login]`

## DESCRIPTION
`server7` is the bootstrap display-server daemon entrypoint.

In default mode it daemonizes, detaches from the invoking terminal,
claims display ownership via `/proc/server7`, listens on loopback TCP,
and serves a minimal native protocol handshake for early client integration.

This command is intended to be started from init runlevel scripts in the
`qemu-server7` boot profile.

## OPTIONS
- `-f` run in foreground (no daemonize)
- `-p port` set loopback listen port (default: `6007`)
- `-m desktop|login` override startup flow policy
  - `desktop`: draw desktop directly
  - `login`: force login-dialog flow

Default flow policy:
- authenticated terminal user session (`uid > 0` with tty) -> desktop-direct
- init/system launch -> login-dialog

## KERNEL CONTROL PATH
`server7` uses `/proc/server7`:flow=... ...`
- client sends `STATUS` -> server returns flow/session metadata pluship
- write `release` to relinquish ownership
- read for status fields (`owner_pid`, `claimed`, `input_events`)

## PROTOCOL (TRANCHE S1)
TCP loopback endpoint, line-oriented:
- client sends `HELLO server7/1` -> server responds `OK proto=1 ...`
- client sends `STATUS` -> server returns `/proc/server7` status text
- client sends `PING` -> server replies `PONG`

## EXIT STATUS
- `0` clean shutdown
- `1` startup failure (argument, daemonize, socket, bind, or listen error)

## EXAMPLES
Start in foreground for debugging:

```sh
server7 -f
```

Start with a custom port:

```sh
server7 -p 6010
```
