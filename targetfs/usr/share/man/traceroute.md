# traceroute(1)

## Name
traceroute - print the route packets take to a network host

## Synopsis
```
traceroute host [max_hops]
```

## Description
`traceroute` traces the path that IP packets take to a destination host by
sending ICMP echo request (PING) probes with increasing IP TTL values. Each
intermediate router that drops a TTL-expired packet responds with an ICMP
TIME_EXCEEDED message, revealing its address. When the destination is reached
it responds with an ICMP ECHO_REPLY.

Three probes are sent per TTL hop. For each probe the round-trip time is
printed. A `*` is printed if no response arrives within the timeout window.

## Arguments
- `host` — Destination host. Accepts a dotted-decimal IPv4 address or a
  hostname (resolved via DNS).
- `max_hops` — Maximum TTL to probe. Default is 30. Range: 1–255.

## Output
```
traceroute to 10.0.2.2, 30 hops max, 26 byte packets
 1  10.0.2.2 <1 ms  10.0.2.2 <1 ms  10.0.2.2 <1 ms
```

Each output line contains the hop number followed by the router/host address
and RTT for each probe. `*` indicates a probe that timed out.

## Notes
- Requires raw ICMP socket support; uses SOCK_RAW/IPPROTO_ICMP.
- Per-hop TTL is set via `setsockopt(IP_TTL)`.
- Intermediate routers must send ICMP TIME_EXCEEDED for intermediate hops
  to be visible. QEMU's NAT/user networking responds correctly.
- Only ICMP echo mode is implemented. UDP and TCP probe modes are not
  currently supported.
- Press Ctrl+C to stop early.

**FIXME:** Intermediate hops are not currently visible when running under
QEMU user-mode networking (SLIRP). SLIRP proxies all outbound packets at the
host level and does not simulate TTL expiry from intermediate routers, so every
destination resolves to hop 1 regardless of real path length. To see
multi-hop output, the guest would need to run on a network with real IP
routing (e.g. tap/bridge mode with actual routers in path) or the SLIRP
implementation would need to synthesise TIME_EXCEEDED responses per hop.

## Examples
```
traceroute 10.0.2.2
traceroute gateway.local 15
traceroute 8.8.8.8
```

## See Also
ping(1), netstat(1), route(1)

## Source Audit
- Source file: user/traceroute.c
- Last updated: 2026-04-03
