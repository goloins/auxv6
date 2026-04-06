# tuntest(1)

## Name
tuntest - Tun/tap regression utility for queue readiness, tun ICMP self-test, and tap ARP self-test coverage.

## Synopsis
```
tuntest nonblock-empty <ifname>
tuntest poll-empty <ifname> [timeout-ms]
tuntest icmp-self <ifname> <local-ip> <peer-ip>
tuntest run-all <ifname> <local-ip> <peer-ip>
tuntest tap-arp-self <ifname> <local-ip> <peer-ip>
tuntest run-all-tap <ifname> <local-ip> <peer-ip>
```

## Duty
Bind tun/tap interfaces through `/dev/net/tun` and exercise queue-readiness
semantics plus self-contained L3/L2 datapath checks:
- tun ICMP self-test injects an IPv4 echo request and validates the queued
  echo reply.
- tap ARP self-test injects an Ethernet+ARP request frame and validates the
  queued ARP reply frame.

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
- `tap-arp-self <ifname> <local-ip> <peer-ip>` — Inject a broadcast ARP
  request frame on TAP, wait for kernel ARP reply, validate Ethernet+ARP
  header/body fields, and confirm `/proc/net_dev` packet counters advance.
- `run-all-tap <ifname> <local-ip> <peer-ip>` — Run empty nonblock, empty
  poll, and `tap-arp-self` checks in sequence.

## Notes
- `tuntest` expects the interface to already exist and be visible via
  `ifconfig`; create it first with `tuntapctl create tun <ifname>`.
- For `icmp-self`, `local-ip` must already be assigned to `<ifname>` with
  `ifconfig`.
- For `tap-arp-self`, `local-ip` must already be assigned to `<ifname>` with
  `ifconfig`.
- `peer-ip` is synthetic test input used as the source address of the injected
  echo request or ARP probe sender IP. No external peer or host integration is
  required.
- Counter validation uses `/proc/net_dev`, so the test also confirms the
  observability surface for the interface under test.

## Examples
```
devman -s
tuntapctl create tun tun0
ifconfig tun0 10.0.0.1 netmask 255.255.255.0
tuntest nonblock-empty tun0
tuntest poll-empty tun0
tuntest icmp-self tun0 10.0.0.1 10.0.0.2
tuntest run-all tun0 10.0.0.1 10.0.0.2
tuntapctl create tap tap0
ifconfig tap0 10.10.0.1 netmask 255.255.255.0
tuntest tap-arp-self tap0 10.10.0.1 10.10.0.2
tuntest run-all-tap tap0 10.10.0.1 10.10.0.2
tuntapctl destroy tap0
tuntapctl destroy tun tun0
```

## Source Audit
- Source file: user/tuntest.c
- Last updated: 2026-04-05