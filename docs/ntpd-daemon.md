# auxv6 NTP Daemon (`ntpd`)

## Overview
auxv6 now includes a lightweight userland `ntpd` daemon that synchronizes
`CLOCK_REALTIME` over UDP NTP and applies updates through
`clock_settime(CLOCK_REALTIME, ...)`.

## Boot Integration
Runlevel `3` starts networking and then `ntpd`:

- `rc.3` runs `/bin/v6dhcpd` first to acquire interface configuration.
- `rc.3` then runs `/bin/ntpd`.
- `ntpd` daemonizes by default, so `rc.3` continues immediately.

## Implementation Notes
- Kernel `clock_gettime` now supports:
  - `CLOCK_MONOTONIC` (existing path)
  - `CLOCK_REALTIME` (new realtime path)
- Kernel now supports `clock_settime(CLOCK_REALTIME, ...)` for privileged
  callers.
- libc `clock_settime` is wired to the new syscall wrapper and enforces
  root-only use in userland before syscall.
- libc `gettimeofday`/`clock_gettime(CLOCK_REALTIME)` prefer kernel realtime.

## `ntpd` Behavior
- Defaults to server `pool.ntp.org`.
- Default successful sync interval: 900 seconds.
- Retry interval after failures: 30 seconds.
- Uses simple client-mode NTP request/response (UDP/123).

## Manual Verification
From an auxv6 shell:

```sh
ps
date
ntpd -f -i 60 pool.ntp.org
date
```

Expected:
- `ps` should include `ntpd` after runlevel 3 startup.
- `date` should reflect network-synchronized wall time after a successful sync.

## Operational Notes
- `ntpd` requires root privileges to update `CLOCK_REALTIME`.
- If DNS is unavailable at startup, use an IPv4 server argument or ensure
  resolver/network configuration is complete before starting `ntpd`.
