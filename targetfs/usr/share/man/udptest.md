# udptest(1)

## Name
udptest – exercise sendto / recvfrom syscalls over the loopback interface.

## Synopsis
- udptest

## Duty
Run an in-guest regression suite verifying the `sendto` and `recvfrom` datagram syscalls.  Three test groups are executed in sequence:

1. **Auto-bind** – sends from an unbound UDP socket; the kernel auto-assigns an ephemeral source port.  `recvfrom` on the server verifies the payload and the filled-in source address.
2. **Round-trip + NULL src** – both sockets are explicitly bound.  The server receives a request via `recvfrom`, replies with `sendto`, and the client calls `recvfrom(NULL, NULL)` to receive the reply without capturing the sender address.
3. **Connected socket** – the client calls `connect` to set `remote_addr`, then invokes `sendto` with a NULL destination pointer, relying on the connected remote address for routing.

Each check prints `PASS` or `FAIL`.  The final line is:

    udptest: PASS N checks
    udptest: FAIL N/M checks failed

## Notes
- Requires a loopback interface (`lo0`) to be up.  Under the standard `make qemu` target the loopback is configured by `/etc/rc.local`.
- UDP ports 33001–33005 are used; ensure nothing else is bound to them before running.
- `flags` to `sendto` / `recvfrom` must be 0 (no MSG_* flags are supported yet).

## Examples
- udptest

## Source Audit
- Source file: user/udptest.c
- Syscalls exercised: sendto(85), recvfrom(86)
- Last updated: 2025-07-20
