# ntpd(1)

## Name
ntpd - lightweight NTP daemon for auxv6.

## Synopsis
```sh
ntpd [-f] [-i interval_seconds] [server]
```

## Duty
Runs as a background daemon (by default), periodically querying an NTP server
over UDP and applying wall-clock updates via `clock_settime(CLOCK_REALTIME)`.

## Options
- `-f` — Stay in foreground (do not daemonize).
- `-i interval_seconds` — Poll interval after a successful sync. Minimum is 30
  seconds. Default is 900 seconds.

## Arguments
- `server` — NTP server hostname or IPv4 address. Default is `pool.ntp.org`.

## Notes
- `ntpd` must run as root because setting system realtime requires privilege.
- On failed sync attempts, `ntpd` retries every 30 seconds.
- In default boot flow, runlevel 3 starts `v6dhcpd` first, then starts `ntpd`.

## Examples
```sh
ntpd
ntpd time.cloudflare.com
ntpd -f -i 120 129.6.15.28
```

## Source Audit
- Source file: user/ntpd.c
- Last updated: 2026-04-03
