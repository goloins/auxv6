# nslookup(1)

## Name
nslookup - Look up DNS hostnames.

## Synopsis
```
nslookup host [server]
```

## Duty
Resolve a hostname to its IP address using DNS. Queries the system-configured
nameserver, or a specified server if provided.

## Options
None.

## Arguments
- `host` — Hostname to resolve (e.g. `example.com`).
- `server` — Optional IP address of the DNS server to query. If omitted,
  the configured nameserver from `/etc/resolv.conf` is used.

## Notes
- Only forward (name → IP) lookups are performed.
- Returns up to the first address found for a given hostname.

## Examples
```
nslookup example.com
nslookup myhost.local 192.168.1.1
```

## Source Audit
- Source file: user/nslookup.c
- Last updated: 2026-04-02
