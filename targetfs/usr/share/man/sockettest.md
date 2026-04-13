# sockettest(1)

## Name
sockettest - UDP socket API regression test.

## Synopsis
```
sockettest
```

## Duty
Test basic UDP socket operations over the loopback interface. Verifies
socket creation, binding, and send/receive round-trips.

## Options
None.

## Tests Performed
1. **Socket creation** — Creates a UDP socket.
2. **Bind** — Binds to a loopback address and port.
3. **Send/receive** — Sends a datagram and reads it back.
4. **Cleanup** — Closes sockets and verifies no leaks.

## Notes
- Requires the loopback interface (`lo0`) to be up.

## Examples
```
sockettest
```

## Source Audit
- Source file: user/sockettest.c
- Last updated: 2026-04-02
