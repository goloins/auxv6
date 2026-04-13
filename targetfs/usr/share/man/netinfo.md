# netinfo(1)

## Name
netinfo - Display network stack summary.

## Synopsis
```
netinfo
```

## Duty
Print a comprehensive summary of the network configuration: interface list,
assigned addresses and flags, routing table, and ARP cache.

## Options
None.

## Output Sections
1. **Interfaces** — Each interface name, flags, MTU, and assigned IP address.
2. **Routes** — Kernel routing table (destination, mask, gateway, interface).
3. **ARP table** — IP-to-MAC mappings.

## Notes
- Similar to running `ifconfig` + `route` + `arp -a` together.

## Examples
```
netinfo
```

## Source Audit
- Source file: user/netinfo.c
- Last updated: 2026-04-02
