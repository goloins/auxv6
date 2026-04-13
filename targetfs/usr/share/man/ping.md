# ping(1)

## Name
ping - Send ICMP echo requests to a host.

## Synopsis
```
ping [ipv4-or-hostname]
```

## Description
Send ICMP echo request packets to the target host and report round-trip times.
Resolves hostnames via DNS if needed. Defaults to the loopback address
(`127.0.0.1`) if no argument is given.

`ping` runs continuously until interrupted with Ctrl+C (SIGINT). When it
receives the signal it prints a statistics summary and exits.

## Options
None.

## Arguments
- `ipv4-or-hostname` — Target host. Accepts a dotted-decimal IPv4 address
  or a hostname (resolved via DNS). Defaults to `127.0.0.1`.

## Output
For each reply received, prints the source address, sequence number, and
round-trip time in milliseconds. On exit, a summary of packets
transmitted, received, and packet loss percentage is printed, along with
min/avg/max RTT statistics.

Round-trip times are measured with the kernel monotonic clock.

## Notes
- Runs continuously until killed with Ctrl+C (SIGINT).
- Uses raw ICMP sockets; may require appropriate privileges.
- One packet is sent per second (10 timer ticks at 100 Hz).

## Examples
```
ping
ping 192.168.1.1
ping gateway.local
```

## See Also
traceroute(1), netstat(1)

## Source Audit
- Source file: user/ping.c
- Last updated: 2026-04-03
