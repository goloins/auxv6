# Network Stack Improvements

This document describes improvements made to the auxv6 network stack during the
network audit pass.

---

## Bugs Fixed

### virtio-net: No poll fallback when IRQ delivery is delayed or missed

**File:** `kernel/driver/virtio_net.c`

`virtio-net` only exposed `if_output` in its `ifnet_ops`. If the host/QEMU
environment delivered virtio interrupts unreliably, completions could stall even
though the interface attached successfully.

**Fix:** Added `virtio_net_poll(struct ifnet *ifp)` and wired it into
`ifnet_ops.if_poll`. The poll path runs the same TX/RX completion handlers used
by the interrupt path, so timer-driven `netdev_poll()` can drain queues when
interrupt delivery is delayed.

### ARP: `ticks` data race in `arp_resolve()` and `arp_input()`

**File:** `kernel/net/arp.c`

`ticks` was read directly inside `arptab.lock` without holding `tickslock`,
creating an ordering violation (rule: never acquire `tickslock` while already
holding another lock).

**Fix:** In both `arp_resolve()` and `arp_input()`, snapshot `ticks` into a
local `now` variable under `tickslock` *before* acquiring `arptab.lock`, then
use `now` for all expire comparisons and assignments.

---

### TCP: Duplicate ISN counter and ephemeral port allocator

**File:** `kernel/net/tcp.c`, `kernel/net/socket.c`

`socket.c` had `static uint tcp_iss = 1000` and a private `alloc_ephemeral_port_locked()`.
`tcp.c` duplicated both as `static uint tcp_iss = 50000` and `tcp_alloc_ephemeral_locked()`.
The connect path in `tcp.c` used its own copy, drifting from the socket layer.

**Fix:**
- `tcp_iss` is now a single non-static `uint` defined in `socket.c`.
- `tcp.c` uses `extern uint tcp_iss`.
- `socket_alloc_port_locked()` is exported from `socket.c`; `tcp_connect()`
  calls it instead of a private allocator.

---

### TCP: No checksum verification on inbound segments

**File:** `kernel/net/tcp.c`

`tcp_input()` accepted any TCP segment without verifying the checksum, meaning
corrupted or spoofed segments were silently processed.

**Fix:** At the start of `tcp_input()` payload parsing, compute the expected
TCP pseudo-header checksum and discard the segment if it does not match the
received value in the TCP header.

---

### TCP: Socket freed in FIN_WAIT_1 before FIN-ACK arrives (UAF)

**File:** `kernel/net/socket.c`, `kernel/net/tcp.c`

`socket_close()` would call `tcp_close()` and then `socket_deref()` in sequence.
If `socket_deref()` dropped the last reference, the socket memory was freed while
the TCP state machine still held a pointer to it (e.g. in the retransmit path or
when the FIN-ACK arrived).

**Fix — teardown ref protocol:**
1. Before calling `tcp_close()`, bump `s->ref` by one and set `s->tcp.close_pending = 1`.
2. Call `tcp_close()` to send the FIN, then call `socket_deref()` for the *user* ref.
   The extra ref keeps the socket alive.
3. Every path in the TCP state machine that transitions to `TCPS_CLOSED` checks
   `close_pending`: if set, it clears the flag and calls `socket_deref()` after
   releasing `socket_lock`.

Affected paths: RST handler, LAST_ACK→CLOSED, `tcp_timewait_check()`,
`tcp_retransmit_check()` giveup.

---

### TCP: Two-byte send cap applied to all socket types

**File:** `kernel/net/socket.c`

`sys_send()` capped all payloads at `MBUF_SIZE - sizeof(udp_hdr)` (~2040 bytes)
even for TCP sockets.

**Fix:** UDP sockets keep the single-segment cap. TCP sockets loop calling
`tcp_output()` in `TCP_MAX_SEGMENT_DATA`-byte chunks until the entire user buffer
is sent or an error occurs.

---

### TCP: No SYN retransmission

**File:** `kernel/net/tcp.c`

`tcp_slowtimo()` only checked `TCPS_ESTABLISHED` and `TCPS_FIN_WAIT_1` for
retransmission. A SYN that was lost would leave the socket stuck in `TCPS_SYN_SENT`
forever.

**Fix:** Added `tcp_syn_retransmit_ifp()`. When `tcp_slowtimo()` sees a socket in
`TCPS_SYN_SENT` whose `last_send + rto` has elapsed, it retransmits the SYN with
exponential back-off, up to `TCP_MAX_RETRANSMIT` attempts.

---

### TCP: No FIN retransmission in FIN_WAIT_1 / LAST_ACK

**File:** `kernel/net/tcp.c`

A lost FIN left the connection half-open with no recovery path.

**Fix:** Added `tcp_fin_retransmit()`. When `tcp_slowtimo()` sees a socket in
`TCPS_FIN_WAIT_1` or `TCPS_LAST_ACK` with an unacknowledged FIN and no data in
flight, it retransmits the FIN with exponential back-off. On max retransmits the
connection is forcibly closed and the `close_pending` ref is released.

---

### TCP: Out-of-order data silently discarded, no duplicate ACK

**File:** `kernel/net/tcp.c`

When a segment arrived with `seq != rcv_nxt`, `tcp_input()` silently dropped it
without sending a duplicate ACK. This prevented the sender from triggering fast
retransmit.

**Fix:** Added an early-return path that sends a duplicate ACK (current `rcv_nxt`,
`snd_nxt`, ACK flag) whenever the segment sequence number does not match `rcv_nxt`.

---

### TCP: Receive window not tracked

**File:** `kernel/net/socket.c`, `kernel/net/tcp.c`

`struct tcpcb` had no field for the peer's advertised receive window, so the send
path could not respect it.

**Fix:** Added `snd_wnd` to `struct tcpcb` (`include/socket.h`). Updated
`tcp_input()` ESTABLISHED case to record `net_ntohs(th->win)` into `snd_wnd`
on every incoming ACK.

---

### TCP: `sys_bind()` had no port conflict detection

**File:** `kernel/net/socket.c`

Two sockets could bind the same port without error.

**Fix:** `sys_bind()` now iterates the socket table and rejects the bind if another
socket is already bound to the same port, unless both sockets have `SO_REUSEADDR`
set.

---

### UDP: No ICMP Port Unreachable on delivery failure

**File:** `kernel/net/udp.c`, `kernel/net/icmp.c`

When a UDP datagram arrived for a port with no listening socket, the packet was
silently dropped. RFC 792 requires an ICMP Destination Unreachable / Port
Unreachable to be sent back.

**Fix:**
- Added `icmp_send_unreach()` to `kernel/net/icmp.c` (type 3, RFC 792 format:
  8-byte ICMP header + original IP header + first 8 bytes of original transport).
- `udp_input()` now calls `icmp_send_unreach(ifp, ip, payload, len, ICMP_UNREACH_PORT)`
  when `socket_deliver()` returns -1 (no matching socket).

---

### UDP: Datagram delivery invoked twice in `udp_input()`

**File:** `kernel/net/udp.c`

`udp_input()` called `socket_deliver()` twice for each incoming datagram: once
unconditionally, then again inside the ICMP-unreachable check. This could cause
duplicate/truncated reads in userland and distort protocol parsers that expect a
single datagram per `recv`.

**Fix:** Call `socket_deliver()` exactly once and trigger ICMP Port Unreachable
only when that single delivery attempt fails.

---

### DHCP client: ACK packets rejected when `yiaddr` is zero

**File:** `user/v6dhcpd.c`

`dhcp_parse_offer()` previously rejected any DHCP message with `yiaddr == 0`.
Some DHCP servers send valid ACK responses with `yiaddr` unset. The client
already had fallback logic in `lease_from_offer()` for this case, but parser
validation dropped those ACKs before they could be applied.

**Fix:** Require non-zero `yiaddr` only for `DHCPOFFER`. Accept `DHCPACK`
with zero `yiaddr` and rely on existing fallback to the offer address.

---

## New Syscalls

Three new POSIX-standard socket syscalls were added:

| Syscall        | Number | Description |
|----------------|--------|-------------|
| `shutdown`     | 91     | Half-close a TCP connection (SHUT_RD / SHUT_WR / SHUT_RDWR) |
| `getsockname`  | 92     | Query the local address/port of a socket |
| `getpeername`  | 93     | Query the remote address/port of a connected socket |

Files changed: `include/syscall.h`, `kernel/core/syscall.c`, `user/usys.S`,
`include/auxv6/user.h`.

### `shutdown(int sockfd, int how)`

- `SHUT_RD`: marks `shut_rd`; recv calls return immediately with 0 bytes.
- `SHUT_WR`: marks `shut_wr`; send calls return `-EPIPE`; sends TCP FIN using
  the teardown-ref protocol (same as `socket_close()` for TCP sockets).
- `SHUT_RDWR`: both of the above.

The file descriptor remains open after `shutdown()`; use `close()` to release it.

### `getsockname(int sockfd, struct sockaddr_in *addr, int *addrlen)`

Returns `s->local_addr`. Works for all socket states after `bind()` or after
a connection is established.

### `getpeername(int sockfd, struct sockaddr_in *addr, int *addrlen)`

Returns `s->remote_addr`. Only valid when the socket is in `SOCK_ESTAB` or
`SOCK_CONNECT` state; returns `-ENOTCONN` otherwise.

---

## New Socket Options

Added to `include/socket.h` (SOL_SOCKET level):

| Constant        | Value | Notes |
|-----------------|-------|-------|
| `SO_REUSEADDR`  | 2     | Allow re-binding a port in TIME_WAIT |
| `SO_KEEPALIVE`  | 9     | Accepted by setsockopt (no keepalive probes yet) |
| `SO_ERROR`      | 4     | Read-only; not yet implemented in getsockopt |
| `SO_BROADCAST`  | 6     | Accepted by setsockopt (no broadcast filter yet) |

`SO_REUSEADDR` is fully functional: `sys_bind()` checks it and `sys_setsockopt()`
/ `sys_getsockopt()` handle it.

---

## `sockaddr_in.sin_family` Type Fix

`sin_family` was `uchar` (1 byte). Changed to `ushort` to match POSIX
(`sa_family_t`). On x86 the binary layout is unchanged because `sin_port` starts
at offset 2 in both cases (the struct is packed by natural alignment, so the extra
byte was padding before).

---

## Shutdown Constants and MSG Flags

Added to `include/socket.h`:

```c
#define SHUT_RD    0
#define SHUT_WR    1
#define SHUT_RDWR  2

#define MSG_DONTWAIT  0x40
#define MSG_PEEK      0x02
#define MSG_WAITALL   0x100
#define MSG_NOSIGNAL  0x4000
```
