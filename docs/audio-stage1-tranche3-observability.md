# Audio Stage 1 Tranche 3 Observability Notes

## Status

Date: 2026-04-05

This document captures Stage 1 Tranche 3 work focused on per-stream observability through procfs and userland status tooling.

Related docs:

- docs/audio-stage1-tranche1-runtime.md
- docs/audio-stage1-tranche2-readiness.md
- docs/audio-subsystem-implementation-plan.md

## Why this tranche exists

After Tranche 1 (runtime stream model) and Tranche 2 (poll/select readiness), the remaining blind spot was per-stream visibility for debugging concurrent clients.

`/proc/audio` and `/proc/audio_stats` gave subsystem-level snapshots, but there was no stream table to answer:

1. which streams are active right now
2. which mode/state each stream is in
3. how full each stream queue is

Tranche 3 fills that gap with `/proc/audio_clients`.

## Files changed

Kernel:

- include/defs.h
- kernel/audio/audio_core.c
- kernel/fs/procfs.c

Userland and docs:

- user/audiostat.c
- targetfs/usr/share/man/audiostat.md
- docs/audio-subsystem-implementation-plan.md
- docs/audio-subsystem-design.md
- docs/ROADMAP.md

## New procfs surface

## `/proc/audio_clients`

A new read-only procfs node exports active stream rows.

Header:

```
slot minor state nonblock used free queued hw sw xruns
```

Each row maps to one in-use stream slot and provides:

1. `slot`: internal stream slot index
2. `minor`: audio device minor associated with the stream
3. `state`: textual state (`new`, `configured`, `prepared`, `running`, `xrun`, `stopped`, `drained`)
4. `nonblock`: `1` if opened nonblocking, else `0`
5. `used`: bytes currently queued in stream ring
6. `free`: bytes available in stream ring
7. `queued`: queued frames
8. `hw`: consumed frames estimate (`hw_ptr`)
9. `sw`: produced frames (`sw_ptr`)
10. `xruns`: per-stream xrun counter

## Implementation details

1. `audio_procfs_clients()` was added in `kernel/audio/audio_core.c`.
2. It iterates only in-use stream slots while holding `audio_core.lock`.
3. For each stream, it updates elapsed-time consume accounting before printing, so queue/ptr values reflect current tick progression.
4. A small state-to-string helper was added for readable output.

## procfs integration

`kernel/fs/procfs.c` now includes:

1. new inode ID for `audio_clients`
2. inode metadata entry in the procfs file table
3. inode fill policy (`0444`, fixed upper bound size)
4. read dispatch hook to `audio_procfs_clients()`

## Userland integration

`audiostat` now reads and prints:

1. `/proc/audio`
2. `/proc/audio_stats`
3. `/proc/audio_clients`

The man page was updated to match this expanded output.

## Behavioral notes

1. This tranche is observability-only: no ABI/ioctl semantic changes.
2. Stream identity in output is slot-based, not PID-based.
3. Slot values are intentionally stable only for the stream lifetime; they can be reused after close.

## Limitations after tranche 3

1. No explicit PID/comm linkage is exported yet in `/proc/audio_clients`.
2. Capture stream visibility will remain minimal until capture path is active.
3. Runtime nonblocking toggles via `F_SETFL` are still not wired into stream behavior.

## Manual smoke sequence

In guest:

1. `devman -s`
2. `audioctl set-params 48000 2 0 256 4 1024 /dev/pcmC0D0p`
3. `audioctl prepare /dev/pcmC0D0p`
4. `audioctl start /dev/pcmC0D0p`
5. `audiotest /dev/pcmC0D0p 64 512`
6. `cat /proc/audio_clients`
7. `audiostat`

Expected result:

- `/proc/audio_clients` prints header plus one row per active stream.
- `used/free/queued/hw/sw` move coherently during active writes.

## Stage mapping

This tranche addresses the Stage 1 observability slice now called out in `docs/audio-subsystem-implementation-plan.md`.

It is a bridge from core runtime readiness work to Stage 2 daemon work by making stream health visible without adding heavyweight tracing first.
