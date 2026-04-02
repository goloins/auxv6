# ping(1)

## Name
ping - Send ICMP echo requests.

## Synopsis
```
ping [ipv4-or-hostname]
```

## Duty
Send 5 ICMP echo request packets to the target host and report round-trip
times. Resolves hostnames via DNS if needed. Defaults to the loopback
address (`127.0.0.1`) if no argument is given.

## Options
None.

## Arguments
- `ipv4-or-hostname` — Target host. Accepts a dotted-decimal IPv4 address
  or a hostname (resolved via DNS). Defaults to `127.0.0.1`.

## Output
For each reply received, prints the round-trip time in microseconds and the
cycle count. Prints a summary of packets sent/received on completion.

## Notes
- Sends exactly 5 packets and then exits.
- Uses raw ICMP sockets; may require appropriate privileges.

## Examples
```
ping
ping 192.168.1.1
ping gateway.local
```

## Source Audit
- Source file: user/ping.c
- Last updated: 2026-04-02
