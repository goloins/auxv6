# auxv6 `6snitch` Design

## Overview

`6snitch` is a planned userland network policy monitor for auxv6. It is inspired by
LittleSnitch-style visibility and alerting, but aligned to auxv6 constraints:

- policy lives in `/etc/6snitch.conf`
- default is disabled
- when enabled, daemon starts from login/session context (not forced at boot)
- alerts carry attribution (pid, app name, uid/gid), network tuple, and rule match metadata

This document defines:

- system architecture and data flow
- config schema for `/etc/6snitch.conf`
- alert record format emitted by `6snitch`
- future procfs interface that gives high-quality attribution data to userland

## Scope And Non-Goals

### MVP Scope

- endpoint and metadata policy matching:
  - process/app
  - protocol
  - direction
  - local/remote ip and port (single values, ranges, CIDR)
- event-driven alerts for connect/bind/listen/accept/sendto policy events
- optional periodic snapshot checks for drift detection
- structured alert output (stdout and/or file)

### Non-Goals For MVP

- deep packet inspection of payload bytes
- TLS interception or certificate introspection
- packet blocking in kernel fast path

## Startup Model

`6snitch` should be integrated as a login-scoped daemon:

- disabled by default in config (`enabled = false`)
- when enabled, a login/session startup hook launches `/bin/6snitch -d`
- daemon enforces single-instance behavior (pidfile or lockfile)
- daemon exits cleanly when session ends

Rationale: this matches "run on login when enabled, off by default" and avoids
surprising system-wide boot impact.

## High-Level Architecture

1. Kernel socket layer exports attributed event/snapshot data via procfs.
2. `6snitch` daemon reads proc streams and parses `/etc/6snitch.conf`.
3. Matching engine evaluates each event against ordered policy rules.
4. Alert formatter emits structured records.
5. Optional helper utility (`6snitchctl`) manages runtime status and reloading.

## `/etc/6snitch.conf` Schema

The configuration format is line-oriented key/value plus rule blocks. It is
human-editable and intentionally strict.

### Lexical Rules

- `#` starts a comment until end of line.
- blank lines are ignored.
- keys are case-sensitive.
- booleans: `true` or `false`.
- lists use commas.
- strings with spaces are double-quoted.
- unknown keys are hard errors.

### Top-Level Keys

- `enabled` (`bool`, default `false`)
- `mode` (`observe` or `enforce`, default `observe`)
- `alert_output` (`stdout`, `file:<path>`, or `both`)
- `alert_format` (`kv1` or `jsonl`, default `kv1`)
- `rate_limit_per_sec` (`uint`, default `20`)
- `burst_limit` (`uint`, default `100`)
- `snapshot_interval_ms` (`uint`, default `2000`)
- `include_loopback` (`bool`, default `false`)
- `resolve_names` (`bool`, default `false`)

Notes:

- MVP should treat `mode = enforce` as reserved unless deny actions are wired.
- `resolve_names = true` is best-effort and must never block event handling.

### Rule Block Schema

Rule blocks are evaluated in order; first match wins.

Block grammar:

```
rule "<rule_name>" {
  action       = alert | allow | deny
  when         = connect | bind | listen | accept | sendto | recvfrom | any
  proto        = tcp | udp | raw | any
  direction    = outbound | inbound | any
  app          = <glob> | any
  pid          = <int> | any
  uid          = <int> | any
  gid          = <int> | any
  local_ip     = <ip|cidr|range|any>
  local_port   = <port|range|list|any>
  remote_ip    = <ip|cidr|range|any>
  remote_port  = <port|range|list|any>
  note         = "free text"
}
```

Matching details:

- `app` matches executable basename (and optionally comm name if present).
- `range` syntax: `a.b.c.d-e.f.g.h` for IP and `start-end` for ports.
- list syntax: `80,443,8080`.
- CIDR syntax: `x.x.x.x/n`.

Action semantics:

- `alert`: emit alert record.
- `allow`: no alert by default, but can be logged in verbose mode.
- `deny`: reserved for future enforcement path (MVP may downgrade to alert).

### Example Config

```
# /etc/6snitch.conf
enabled = true
mode = observe
alert_output = file:/var/log/6snitch.log
alert_format = kv1
rate_limit_per_sec = 25
burst_limit = 100
snapshot_interval_ms = 1000
include_loopback = false
resolve_names = false

rule "curl-to-public-web" {
  action = alert
  when = connect
  proto = tcp
  direction = outbound
  app = "curl"
  remote_ip = 0.0.0.0/0
  remote_port = 80,443
  note = "interactive http(s) egress"
}

rule "dns-except-local-resolver" {
  action = alert
  when = sendto
  proto = udp
  app = any
  remote_ip = 0.0.0.0/0
  remote_port = 53
  note = "unexpected dns egress"
}

rule "default-observe" {
  action = allow
  when = any
  proto = any
  direction = any
  app = any
  local_ip = any
  local_port = any
  remote_ip = any
  remote_port = any
  note = "fallthrough"
}
```

## Alert Record Format

`6snitch` emits one record per matched event.

### Format Choice

Two output encodings are supported:

- `kv1` (default): single-line key=value pairs, easy for grep/awk
- `jsonl`: one JSON object per line for external tooling

The semantic fields are identical across formats.

### Required Fields

- `ver` format version (`1`)
- `ts_mono_ms` monotonic timestamp in ms
- `ts_real_s` realtime unix epoch seconds (if available)
- `seq` daemon-local event sequence number
- `rule` matched rule name
- `action` matched action (`alert|allow|deny`)
- `event` kernel event type (`connect|bind|listen|accept|sendto|recvfrom|snapshot`)
- `proto` (`tcp|udp|raw`)
- `dir` (`in|out|na`)
- `pid`
- `ppid` (if exported)
- `uid`
- `gid`
- `comm` process short name
- `exe` executable path if available, else `unknown`
- `laddr` local address
- `lport` local port
- `raddr` remote address
- `rport` remote port
- `sock_state` socket state string
- `sock_id` stable kernel socket id (if exported)
- `note` rule note string (optional)

### Optional Diagnostic Fields

- `ifname` interface name
- `netns` namespace id (future)
- `rxq` receive queue bytes
- `txq` send queue bytes
- `drop_reason` if event was dropped/sampled

### `kv1` Example

```
ver=1 ts_mono_ms=834221 ts_real_s=1775705402 seq=412 rule="dns-except-local-resolver" action=alert event=sendto proto=udp dir=out pid=73 ppid=1 uid=0 gid=0 comm="netcat" exe="/bin/netcat" laddr=10.0.2.15 lport=49152 raddr=1.1.1.1 rport=53 sock_state=BOUND sock_id=228 note="unexpected dns egress"
```

### `jsonl` Example

```
{"ver":1,"ts_mono_ms":834221,"ts_real_s":1775705402,"seq":412,"rule":"dns-except-local-resolver","action":"alert","event":"sendto","proto":"udp","dir":"out","pid":73,"ppid":1,"uid":0,"gid":0,"comm":"netcat","exe":"/bin/netcat","laddr":"10.0.2.15","lport":49152,"raddr":"1.1.1.1","rport":53,"sock_state":"BOUND","sock_id":228,"note":"unexpected dns egress"}
```

## Future Proc Interface For Best Attribution

Current `/proc/net_*` snapshots are useful but insufficient for robust
attribution and short-lived flow detection. The following procfs contract is
proposed for `6snitch`.

### Goals

- event stream with low latency
- stable process/socket attribution
- bounded memory and lock-safe snapshots
- userland-friendly text format first, binary optional later

### Proposed Proc Nodes

1. `/proc/6snitch_events`
- read-only event stream (line records)
- supports blocking reads
- each line represents one socket lifecycle event with attribution

2. `/proc/6snitch_sockets`
- read-only snapshot table of currently active sockets
- includes owner metadata and socket id

3. `/proc/6snitch_stats`
- read-only counters:
  - events_emitted
  - events_dropped
  - queue_highwater
  - snapshot_calls
  - snapshot_failures

4. `/proc/6snitch_ctrl`
- write-only lightweight control surface:
  - `reset_stats`
  - `set_queue_limit <n>`
  - `set_event_mask <mask>`

### `/proc/6snitch_events` Record Schema

Each event line should include:

- `seq` monotonically increasing kernel event id
- `ticks` kernel tick timestamp
- `event` connect/bind/listen/accept/sendto/recvfrom/close/state
- `sock_id` stable socket identifier
- `proto`
- `family`
- `pid`, `ppid`, `uid`, `gid`
- `comm`, `exe_inode` (or executable identifier)
- `laddr`, `lport`, `raddr`, `rport`
- `tcp_state` for stream sockets
- `rxq`, `txq`
- `ifindex` when known

Process attribution expectations:

- owner identity captured at socket creation and updated on ownership transfer
  only when explicit kernel handoff occurs
- pid reuse safety via tuple `(pid, start_ticks)` if available
- if attribution is unknown, emit explicit sentinel values (not omitted fields)

### `/proc/6snitch_sockets` Snapshot Schema

Header example:

```
SOCK_ID PID UID GID COMM PROTO STATE LADDR LPORT RADDR RPORT RXQ TXQ CTIME
```

This table should be cheap enough for periodic reconciliation and daemon
startup resync.

### Compatibility Notes

- Existing `/proc/net_tcp` and `/proc/net_udp` remain unchanged for current
  tools (`netstat`, etc.).
- `6snitch` should prefer `/proc/6snitch_*` when present and fall back to
  legacy `/proc/net_*` with degraded attribution.

## Reliability And Safety Requirements

- bounded kernel queue for events; overflow increments drop counter
- strict parser in userland config loader; fail closed on malformed rules
  (daemon does not run with partial config unless explicitly configured)
- alert rate limiting at daemon output stage
- no blocking DNS/process-name lookups in kernel path

## Implementation Phasing Summary

1. Add kernel attribution fields and stable `sock_id` lifecycle.
2. Add `/proc/6snitch_events` and `/proc/6snitch_sockets`.
3. Implement userland parser/matcher/alerter around `/etc/6snitch.conf`.
4. Add login-scoped startup hook with `enabled=false` default.
5. Add operational docs/man page and validation tests.
