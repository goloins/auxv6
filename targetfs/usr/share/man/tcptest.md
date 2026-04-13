# tcptest(1)

## Name
tcptest - TCP socket regression test.

## Synopsis
```
tcptest
```

## Duty
Test TCP socket functionality. Creates a server socket, accepts a client
connection, and verifies bidirectional message exchange.

## Options
None.

## Tests Performed
1. **Server socket** — Creates and binds a TCP listening socket.
2. **Client connect** — Forks a child that connects to the server.
3. **Message exchange** — Client sends a message; server reads and replies.
4. **Cleanup** — Closes sockets and waits for child.

## Notes
- Uses loopback interface (`127.0.0.1`).

## Examples
```
tcptest
```

## Source Audit
- Source file: user/tcptest.c
- Last updated: 2026-04-02
