# tuntest(1)

## Name
tuntest - TUN Phase 1 regression utility for nonblocking, poll, and ICMP self-test coverage.

## Synopsis
```
tuntest nonblock-empty <ifname>
tuntest poll-empty <ifname> [timeout-ms]
tuntest icmp-self <ifname> <local-ip> <peer-ip>
tuntest run-all <ifname> <local-ip> <peer-ip>
```

## Duty
Bind a tun interface through `/dev/net/tun` and exercise the Phase 1 data-path
contract: empty-queue `O_NONBLOCK` reads, empty-queue poll readiness, and an
end-to-end ICMP echo path where userspace injects an IPv4 echo request and
validates the kernel-generated echo reply read back from the tun fd.

## Commands
- `nonblock-empty <ifname>` — Open and bind the named tun interface, enable
  `O_NONBLOCK`, and verify an empty read fails immediately.
- `poll-empty <ifname> [timeout-ms]` — Verify an empty queue reports writable
  readiness without readable or error readiness.
- `icmp-self <ifname> <local-ip> <peer-ip>` — Inject an ICMP echo request from
  `peer-ip` to `local-ip`, wait for the reply, validate the IPv4 and ICMP
  headers, and confirm `/proc/net_dev` packet counters advance.
- `run-all <ifname> <local-ip> <peer-ip>` — Run the three checks above in
  sequence and stop on first failure.

## Notes
- `tuntest` expects the interface to already exist and be visible via
  `ifconfig`; create it first with `tuntapctl create tun <ifname>`.
- For `icmp-self`, `local-ip` must already be assigned to `<ifname>` with
  `ifconfig`.
- `peer-ip` is synthetic test input used as the source address of the injected
  echo request. No external tunnel peer or host integration is required.
- Counter validation uses `/proc/net_dev`, so the test also confirms the
  observability surface for the tun interface.
- This utility validates TUN/L3 behavior only. TAP/L2 validation remains a
  later tranche.

## Examples
```
devman -s
tuntapctl create tun tun0
ifconfig tun0 10.0.0.1 netmask 255.255.255.0
tuntest nonblock-empty tun0
tuntest poll-empty tun0
tuntest icmp-self tun0 10.0.0.1 10.0.0.2
tuntest run-all tun0 10.0.0.1 10.0.0.2
tuntapctl destroy tun tun0
```

## Source Audit
- Source file: user/tuntest.c
- Last updated: 2026-04-05