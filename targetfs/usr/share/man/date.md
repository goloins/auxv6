# date(1)

## Name
date - Print current date and time.

## Synopsis
```
date [-u]
```

## Description
Read the hardware real-time clock (RTC) and print the date and time. By
default, the local time is reported using the timezone configured in
`/etc/timezone`. The timezone offset is resolved by consulting the table in
`/usr/share/zoneinfo/zones.tab`.

If `/etc/timezone` is absent or names an unknown zone, `date` falls back
silently to UTC.

## Options
```
-u, --utc   Print UTC time regardless of the configured timezone.
```

## Files
- `/etc/timezone` — one-line file containing the system timezone name,
  e.g. `America/New_York` or `UTC`.
- `/usr/share/zoneinfo/zones.tab` — tab-separated timezone table with
  columns: name, UTC-offset-in-seconds, abbreviation.

## Output
Outputs a single line in the format:
```
YYYY-MM-DD HH:MM:SS TZ
```
where `TZ` is the timezone abbreviation (e.g. `EST`, `UTC`).

## Examples
```
# Default: local time (timezone from /etc/timezone)
date
2026-04-02 09:30:00 EST

# UTC time
date -u
2026-04-02 14:30:00 UTC
```

## Source Audit
- Source file: user/date.c
- Config: targetfs/etc/timezone
- Zone table: targetfs/usr/share/zoneinfo/zones.tab
- Last updated: 2026-04-03
