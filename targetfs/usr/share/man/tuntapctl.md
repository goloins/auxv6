# tuntapctl(1)

## Name
tuntapctl - Create and manage `/dev/net/tun` virtual interface bindings.

## Synopsis
```
tuntapctl create <tun|tap> [name]
tuntapctl destroy <tun|tap> <name>
tuntapctl get <tun|tap> <name>
tuntapctl persist <tun|tap> <name> <0|1>
tuntapctl owner <tun|tap> <name> <uid>
tuntapctl group <tun|tap> <name> <gid>
```

## Duty
Open `/dev/net/tun`, bind or create a virtual interface with `TUNSETIFF`, and
issue baseline tun/tap control ioctls for state query and ownership/persistence
policy.

## Commands
- `create <tun|tap> [name]` — Create or bind a tun/tap unit. If `name` is
  omitted, kernel-side auto-allocation is used. Automatically sets
  `TUNSETPERSIST=1` so the kernel network interface survives after this fd closes.
  The created interface is immediately visible via `ifconfig`.
- `destroy <tun|tap> <name>` — Bind the named unit and set `TUNSETPERSIST=0`,
  allowing the kernel to release the unit once all fds are closed.
- `get <tun|tap> <name>` — Bind the named unit and print current mode flags
  via `TUNGETIFF`.
- `persist <tun|tap> <name> <0|1>` — Set `TUNSETPERSIST` for the bound unit.
- `owner <tun|tap> <name> <uid>` — Set `TUNSETOWNER` for the bound unit.
- `group <tun|tap> <name> <gid>` — Set `TUNSETGROUP` for the bound unit.

## Notes
- `tuntapctl` always opens `/dev/net/tun` and requires that node to exist
  (typically created by `devman -s`).
- The utility requests `IFF_NO_PI` when binding/creating units.
- Mode must be either `tun` or `tap`.
- `tun0` is a kernel virtual network interface and is NOT visible as a file
  under `/dev/net/`. After `create`, use `ifconfig` to see it and assign an
  address.
- To transfer packets through a tun/tap interface, a process must open
  `/dev/net/tun`, call `TUNSETIFF <name>`, and keep the fd open for I/O.
  `tuntapctl create` registers the interface; it does not serve as the I/O
  endpoint.
- Permission checks and exact ioctl semantics are enforced by the kernel
  tuntap driver.

## Examples
```
devman -s
tuntapctl create tun
tuntapctl create tun tun0
ifconfig tun0 10.0.0.1 netmask 255.255.255.0
tuntapctl get tun tun0
tuntapctl persist tun tun0 1
tuntapctl owner tun tun0 1000
tuntapctl group tun tun0 1000
tuntapctl destroy tun tun0
```

## Source Audit
- Source file: user/tuntapctl.c
- Last updated: 2026-04-05