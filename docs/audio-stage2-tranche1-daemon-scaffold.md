# Audio Stage 2 Tranche 1 Daemon Scaffold Notes

## Status

Date: 2026-04-05

This document captures the first Stage 2 landing for the audio subsystem: a minimal audiod daemon that services one native PCM playback stream.

Related docs:

- docs/audio-subsystem-design.md
- docs/audio-subsystem-implementation-plan.md
- docs/audio-stage1-tranche3-observability.md

## Why this tranche exists

After Stage 1, kernel-side stream lifecycle/readiness/observability existed, but there was no long-running userspace policy process to hold stream state and execute an event loop.

This tranche establishes that userspace daemon lifecycle with the smallest practical implementation.

## Files changed

Userland:

- user/audiod.c
- targetfs/usr/share/man/audiod.md

Build and packaging:

- Makefile
- .gitignore

Docs:

- docs/audio-subsystem-design.md
- docs/audio-subsystem-implementation-plan.md
- docs/ROADMAP.md
- docs/CHANGELOG-2026-04.md
- docs/man-pages.md
- docs/audio-stage2-tranche1-daemon-scaffold.md (this document)

## Tranche behavior

The new audiod process currently:

1. parses stream configuration arguments
2. opens a single PCM playback endpoint (default `/dev/pcmC0D0p`)
3. issues `AUDIO_IOC_SET_PARAMS`, `AUDIO_IOC_PREPARE`, `AUDIO_IOC_START`
4. enters a `poll(2)` loop for `POLLOUT`/`POLLERR`
5. writes silent PCM payloads on writable events
6. recovers XRUN using `AUDIO_IOC_RESET_XRUN` + `PREPARE` + `START`
7. drains/stops on shutdown

Daemon lifecycle:

- supports `-f` foreground mode for debugging
- defaults to double-fork daemon mode
- handles `SIGTERM`/`SIGINT` for clean termination

## What this tranche is not

1. No client IPC protocol.
2. No software mixer.
3. No multi-stream routing policy.
4. No per-client volume policy.
5. No persistence or session graph.

Those are follow-on Stage 2 slices.

## Manual smoke sequence

Inside guest:

1. `devman -s`
2. `audiod -f -v`
3. In another shell: `audioctl status /dev/pcmC0D0p`
4. `audiostat`
5. Stop daemon with Ctrl-C

Expected high-level signals:

1. daemon starts and configures stream without panic
2. status/queue counters move while daemon runs
3. daemon exits cleanly on signal

## Stage mapping

This tranche begins Stage 2 from docs/audio-subsystem-implementation-plan.md:

- Daemon lifecycle: started
- Event loop and sink servicing: started (single sink)
- Mixer/routing policy: pending
- Multi-client policy surface: pending

## Recommended next Stage 2 tranche

1. Add a minimal local control path (for example, command fd/control endpoint) for live reconfigure and stats.
2. Add multi-stream in-daemon mix loop over multiple producer fds.
3. Add per-stream gain and clip counters.
4. Add a focused audiod soak test profile that exercises xrun recovery under sustained pressure.
