# netstat(1)

## Name
netstat - Display network interfaces and routing table.

## Synopsis
```
netstat [-i | -r | -rn]
```

## Duty
Show network interface statistics and/or the kernel routing table.

## Options
- `-i` — Show network interfaces only (names, flags, addresses).
- `-r` — Show routing table only.
- `-rn` — Show routing table with numeric addresses (same as `-r` in
  this implementation; hostname resolution is not performed).

With no options, both interfaces and the routing table are shown.

## Output (interfaces)
- Interface name, IP address, flags, MTU

## Output (routes)
- Destination, mask, gateway, interface name

## Examples
```
netstat
netstat -i
netstat -r
netstat -rn
```

## Source Audit
- Source file: user/netstat.c
- Last updated: 2026-04-02
