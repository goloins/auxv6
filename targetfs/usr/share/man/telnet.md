# telnet(1)

## Name
telnet - Telnet client.

## Synopsis
```
telnet host [port]
```

## Duty
Connect to a remote host using the Telnet protocol over TCP. Handles the
IAC (Interpret As Command) control sequences and relays terminal I/O to
and from the connection.

## Options
None.

## Arguments
- `host` — Remote hostname or IP address to connect to.
- `port` — TCP port number. Defaults to `23` (standard Telnet port).

## Notes
- IAC protocol negotiation is handled automatically.
- Input from stdin is forwarded to the remote; remote data is written to
  stdout.
- NAWS (Negotiate About Window Size, RFC 1073) is supported. On connect,
  the client sends `WILL NAWS` and, if the server accepts with `DO NAWS`,
  immediately sends the current terminal dimensions. When the terminal is
  resized (SIGWINCH), the updated size is sent to the server automatically.
- Press Ctrl-], Ctrl-C, or Ctrl-Z to terminate the session.

## Examples
```
telnet 192.168.1.5
telnet 10.0.0.1 2323
```

## Source Audit
- Source file: user/telnet.c
- Last updated: 2026-04-03
