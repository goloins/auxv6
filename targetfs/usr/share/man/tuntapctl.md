# tuntapctl(1)

## Name
tuntapctl - Create and manage `/dev/net/tun` virtual interface bindings.

## Synopsis
``` 
tuntapctl create <tun|tap> [name]
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
- `create <tun|tap> [name]` - Create or bind a tun/tap unit. If `name` is
  omitted, kernel-side auto-allocation is used.
- `get <tun|tap> <name>` - Bind the named unit and print current mode flags
  via `TUNGETIFF`.
- `persist <tun|tap> <name> <0|1>` - Set `TUNSETPERSIST` for the bound unit.
- `owner <tun|tap> <name> <uid>` - Set `TUNSETOWNER` for the bound unit.
- `group <tun|tap> <name> <gid>` - Set `TUNSETGROUP` for the bound unit.

## Notes
- `tuntapctl` always opens `/dev/net/tun` and requires that node to exist
  (typically created by `devman -s`).
- The utility requests `IFF_NO_PI` when binding/creating units.
- Mode must be either `tun` or `tap`.
- For `get`, `persist`, `owner`, and `group`, a successful name bind happens
  first using `TUNSETIFF`.
- Permission checks and exact ioctl semantics are enforced by the kernel
  tuntap driver.

## Examples
```
devman -s
tuntapctl create tun
tuntapctl create tun tun0
tuntapctl get tun tun0
tuntapctl persist tun tun0 1
tuntapctl owner tun tun0 1000
tuntapctl group tun tun0 1000
```

## Source Audit
- Source file: user/tuntapctl.c
- Last updated: 2026-04-05