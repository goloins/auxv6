# mlfqtune(1)

## Name
mlfqtune - read or set the MLFQ global boost interval

## Synopsis
```sh
mlfqtune
mlfqtune <ticks>
mlfqtune -m <milliseconds>
```

## Description
`mlfqtune` is a small control utility for the MLFQ scheduler boost interval.
It reads and writes `/proc/mlfq_tune`, which controls the period used by the
scheduler's global anti-starvation boost.

The scheduler tick rate is 100 Hz, so one tick is 10 ms.

## Arguments
- No arguments: print the current `/proc/mlfq_tune` status block.
- `<ticks>`: set `boost_interval_ticks` directly.
- `-m <milliseconds>`: set interval in milliseconds (rounded up to ticks).

## Limits
The kernel enforces a valid range of `10..5000` ticks.

## Examples
```sh
mlfqtune
mlfqtune 200
mlfqtune -m 500
cat /proc/schedstat | grep mlfq_boost_interval_ticks
```

## Exit Status
- `0` success
- `1` invalid argument or read/write failure

## See Also
man(1), schedperf(1)

## Source Audit
- Source file: user/mlfqtune.c
- Last updated: 2026-04-18
