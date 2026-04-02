# ifconfig(1)

## Name
ifconfig - Configure or display network interface settings.

## Synopsis
```
ifconfig
ifconfig ifname
ifconfig ifname addr mask
ifconfig ifname addr netmask mask
ifconfig ifname inet addr netmask mask
```

## Duty
Display or configure IP address and subnet mask for a network interface.
With no arguments, shows all interfaces. With only `ifname`, shows that
interface. With address arguments, sets the interface address.

## Options
- `-h`, `--help` — Print usage and exit.

## Arguments
- `ifname` — Interface name (e.g. `eth0`, `lo0`)
- `addr` — IPv4 address in dotted-decimal notation (e.g. `192.168.1.10`)
- `mask` — Subnet mask in dotted-decimal notation (e.g. `255.255.255.0`)

## Forms
- `ifconfig ifname addr mask` — Set address and mask directly
- `ifconfig ifname addr netmask mask` — BSD-style with `netmask` keyword
- `ifconfig ifname inet addr netmask mask` — Full BSD form with `inet` keyword

## Examples
```
ifconfig
ifconfig eth0
ifconfig eth0 192.168.1.10 255.255.255.0
ifconfig eth0 inet 10.0.0.5 netmask 255.0.0.0
```

## Source Audit
- Source file: user/ifconfig.c
- Last updated: 2026-04-02
