# route(1)

## Name
route - Display or modify the kernel routing table.

## Synopsis
```
route
route add default <gateway> <ifname>
route add <dst> <netmask> <gateway|-> <ifname> [src]
route del default <ifname>
route del <dst> <netmask> <ifname>
```

## Duty
Display and manipulate the kernel IP routing table. Without subcommands,
prints all current routes.

## Options
None.

## Subcommands
- `add default <gateway> <ifname>` — Add a default route via `gateway`
  on interface `ifname`.
- `add <dst> <netmask> <gateway|-> <ifname> [src]` — Add a specific
  host or network route. Use `-` as gateway for directly connected routes.
  An optional source address `src` can be specified.
- `del default <ifname>` — Delete the default route for `ifname`.
- `del <dst> <netmask> <ifname>` — Delete a specific route.

## Arguments
- `dst` — Destination network or host address (dotted-decimal)
- `netmask` — Network mask (dotted-decimal)
- `gateway` — Next-hop IP address, or `-` for a directly connected route
- `ifname` — Interface name (e.g. `eth0`, `lo0`)

## Examples
```
route
route add default 192.168.1.1 eth0
route add 10.0.0.0 255.0.0.0 - lo0
route del default eth0
```

## Source Audit
- Source file: user/route.c
- Last updated: 2026-04-02
