# ping(1)

## Name
ping - Send ICMP ECHO requests to a host.

## Synopsis
```
ping [-4anqv] [-c count] [-i interval] [-W timeout] [-w deadline] [-s packetsize] [-t ttl] destination
ping -h
```

## Description
Send ICMP echo request packets to the target host and report round-trip times.
Resolves hostnames via DNS if needed (unless `-n` is used).

`ping` runs continuously until interrupted with Ctrl+C (SIGINT). When it
receives the signal it prints a statistics summary and exits.

When called with no arguments, `ping` prints usage and exits with an error.

## Options
- `-c count` - Stop after sending `count` requests.
- `-i interval` - Seconds between probes. Fractional values are accepted.
- `-W timeout` - Per-probe reply timeout in seconds. Fractional values are accepted.
- `-w deadline` - Total run deadline in seconds. Fractional values are accepted.
- `-s packetsize` - ICMP payload size in bytes. Default is `56`.
- `-t ttl` - Set outgoing IPv4 TTL (`1..255`) with `setsockopt(IP_TTL)`.
- `-a` - Audible mode. Ring terminal bell on each received reply.
- `-n` - Numeric mode. Do not resolve hostnames.
- `-q` - Quiet output. Suppress per-packet output, show summary only.
- `-v` - Verbose mode. Print extra ICMP diagnostics for non-matching packets.
- `-4` - IPv4 mode (compatibility flag; IPv4 is the only supported family).
- `-h`, `--help` - Show usage.

## Arguments
- `destination` - Target host. Accepts a dotted-decimal IPv4 address
  or a hostname (resolved via DNS unless `-n` is used).

## Output
For each reply received, prints the source address, sequence number, and
round-trip time in milliseconds. On exit, a summary of packets
transmitted, received, and packet loss percentage is printed, along with
min/avg/max/mdev RTT statistics.

Round-trip times are measured with the kernel monotonic clock.

## Notes
- Runs continuously until killed with Ctrl+C (SIGINT).
- Uses raw ICMP sockets; may require appropriate privileges.
- Default cadence is one packet per second.
- Time options use the kernel tick clock (100 Hz), so effective timing
  granularity is 10 ms.

## Compatibility Notes
This implementation supports a practical Linux/BSD-style subset, but does not
currently implement every common flag.

Not currently supported due to missing kernel/socket support:
- `-I interface|source` (interface or source-address pinning): requires
  `SO_BINDTODEVICE` and/or source-specific raw socket binding behavior.
- `-M` PMTU controls / DF-bit policies: requires IP_MTU_DISCOVER style options.
- `-Q tos` / DSCP marking: requires `IP_TOS` socket option support.
- `-R` record-route and `-T` IP timestamp options: require IPv4 IP options support.
- Receive TTL/hoplimit display and ancillary controls: require recvmsg ancillary
  data and IP_RECVTTL-like facilities.

## Examples
```
ping 192.168.1.1
ping gateway.local
ping -c 4 -W 0.5 192.168.1.1
ping -q -c 10 -i 0.2 -s 128 10.0.2.2
ping -a -v -c 3 10.0.2.2
```

## See Also
traceroute(1), netstat(1)

## Source Audit
- Source file: user/ping.c
- Last updated: 2026-04-14
