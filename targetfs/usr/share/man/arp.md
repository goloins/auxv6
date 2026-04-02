# arp(1)

## Name
arp - Display ARP cache entries.

## Synopsis
```
arp [-a]
```

## Duty
Display the ARP (Address Resolution Protocol) table, which maps IPv4
addresses to MAC (hardware) addresses on the local network.

## Options
- `-a` — List all ARP entries. This is the default behavior; specifying
  `-a` is optional.

## Output Columns
- IP address
- MAC address
- Interface name

## Examples
```
arp
arp -a
```

## Source Audit
- Source file: user/arp.c
- Last updated: 2026-04-02
