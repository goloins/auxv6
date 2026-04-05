# Audio Stage 1 Tranche 2 Readiness Notes

## Status

Date: 2026-04-05

This document captures Stage 1 Tranche 2 work focused on poll/select readiness integration for native audio streams.

Related docs:

- docs/audio-stage1-tranche1-runtime.md
- docs/audio-subsystem-implementation-plan.md
- docs/audio-subsystem-design.md

## Why this tranche exists

Stage 1 Tranche 1 added per-fd streams and ring buffering, but poll/select still treated audio descriptors as generic inode fds.

That meant readiness signaling could be inaccurate for stream pressure and xrun conditions.

Tranche 2 adds explicit audio readiness reporting so event-driven userland can observe:

1. writable-space availability
2. stream error state (xrun)

## Files changed

- include/defs.h
- kernel/audio/audio_core.c
- kernel/core/sysfile.c

## Readiness model implemented

## API surface

New kernel internal helper:

- `audio_poll_events(struct file *f, int *rd, int *wr, int *err)`

It mirrors the existing PTY readiness pattern (`pty_poll_events`) and is called by the generic fd readiness scanner used by both `poll(2)` and `select(2)` paths.

## Stream endpoints (/dev/pcmC*D*p)

For stream minors:

1. `wr` is set when per-stream ring has free bytes.
2. `err` is set when stream state is `AUDIO_ST_XRUN`.
3. `rd` remains 0 in this tranche (capture not active yet).

The helper also advances software consume accounting before evaluating readiness so writable status tracks elapsed-time ring drain behavior.

## Control endpoint (/dev/audioctl)

For control minor:

1. `wr` is always ready when fd is writable.
2. `rd` is marked ready when fd is readable.

This keeps control endpoint behavior predictable for command-style clients while stream readiness remains queue-driven.

## Generic poll/select integration

`fd_ready_events()` in `kernel/core/sysfile.c` now has an AUDIODEV branch that:

1. calls `audio_poll_events()`
2. maps helper outputs to poll masks:
- `rd -> POLLIN`
- `wr -> POLLOUT`
- `err -> POLLERR | POLLHUP`

This directly feeds:

- `poll_scan()` used by `sys_poll`
- `select_scan()` used by `sys_select`

## Behavioral notes

1. Tranche 2 does not yet add stream-specific wait channels inside poll loops beyond existing core polling cadence.
2. Writable readiness is ring-space-based and therefore reflects backpressure.
3. Xrun surfaces as poll error so event loops can detect recovery-needed state without extra ioctl roundtrips.

## Limitations after tranche 2

1. Capture readiness (`POLLIN`) is still not implemented for PCM streams.
2. Xrun and recovery semantics are still evolving toward full Stage 1 completion.
3. `F_SETFL` runtime toggling of nonblocking mode is still global-kernel TODO; stream nonblocking currently reflects open-time mode.

## Manual smoke sequence

In guest:

1. `devman -s`
2. `audioctl set-params 48000 2 0 256 4 1024 /dev/pcmC0D0p`
3. `audioctl prepare /dev/pcmC0D0p`
4. `audioctl start /dev/pcmC0D0p`
5. `audiotest /dev/pcmC0D0p 128 512`
6. In a second process, run a poll/select-aware writer (future helper) or repeated `audioctl status` to observe queue pressure and xrun transitions.

## Stage mapping

This tranche addresses Stage 1 task 4 from `docs/audio-subsystem-implementation-plan.md`:

- Readiness integration: wire poll/select readiness events for write-space and state changes.

## Next follow-on recommendations

1. Add explicit userland poll stress utility for audio write readiness.
2. Add `F_SETFL` nonblocking tracking so runtime toggles apply to stream write path.
3. Add capture endpoint scaffolding and `POLLIN` readiness semantics.
