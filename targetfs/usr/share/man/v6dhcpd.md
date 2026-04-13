# v6dhcpd(1)

## Name
v6dhcpd - DHCP client.

## Synopsis
```
v6dhcpd [ifname]
v6dhcpd acquire [ifname]
v6dhcpd renew [ifname]
v6dhcpd release [ifname]
```

## Duty
Obtain, renew, or release a DHCP lease on a network interface. Broadcasts
DHCPDISCOVER, processes DHCPOFFER, sends DHCPREQUEST, and applies the
offered configuration (IP, mask, router). Persists lease to
`/etc/v6dhcpd.IFNAME.lease`.

## Options
None.

## Subcommands
- *(no subcommand)* — Acquire a new lease (same as `acquire`).
- `acquire` — Broadcast DHCPDISCOVER and complete the full DORA handshake
  to obtain a new lease.
- `renew` — Attempt to renew an existing lease by sending DHCPREQUEST
  directly to the server recorded in the lease file.
- `release` — Send DHCPRELEASE to the server and clear the lease.

## Arguments
- `ifname` — Network interface to configure (e.g. `eth0`). If omitted,
  the first non-loopback, up interface is used automatically.

## Notes
- Lease data is stored in `/etc/v6dhcpd.IFNAME.lease`.
- Tries up to 4 times with a timeout before giving up.
- After a successful acquire, the interface address and default route are
  configured automatically.

## Examples
```
v6dhcpd
v6dhcpd eth0
v6dhcpd acquire eth0
v6dhcpd renew eth0
v6dhcpd release eth0
```

## Source Audit
- Source file: user/v6dhcpd.c
- Last updated: 2026-04-02
