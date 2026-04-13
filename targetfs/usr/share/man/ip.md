# ip(1)

## Name
ip - IP address and routing configuration.

## Synopsis
```
ip addr show
ip addr add <addr>/<prefix> dev <ifname>
ip route show
ip route add default via <gateway> dev <ifname>
ip route add <dst>/<prefix> via <gateway|-> dev <ifname>
ip route del default dev <ifname>
ip route del <dst>/<prefix> dev <ifname>
```

## Duty
Display and configure IP addresses and kernel routing table entries.
Supports CIDR notation for addresses and prefixes.

## Options
None. All configuration is expressed as subcommands and positional arguments.

## Subcommands
### addr
- `addr show` — List all interfaces with their addresses, flags, MTU, and MAC.
- `addr add <addr>/<prefix> dev <ifname>` — Assign an IPv4 address with
  CIDR prefix length to an interface.

### route
- `route show` — Display the kernel routing table.
- `route add default via <gateway> dev <ifname>` — Add a default gateway.
- `route add <dst>/<prefix> via <gateway> dev <ifname>` — Add a specific
  route. Use `-` as gateway for directly connected routes.
- `route del default dev <ifname>` — Remove the default route for an
  interface.
- `route del <dst>/<prefix> dev <ifname>` — Remove a specific route.

## Examples
```
ip addr show
ip addr add 192.168.1.10/24 dev eth0
ip route show
ip route add default via 192.168.1.1 dev eth0
ip route add 10.0.0.0/8 via - dev lo0
ip route del default dev eth0
```

## Source Audit
- Source file: user/ip.c
- Last updated: 2026-04-02
