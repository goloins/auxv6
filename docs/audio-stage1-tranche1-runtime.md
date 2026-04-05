# Audio Stage 1 Tranche 1 Runtime Notes

## Status

Date: 2026-04-05

This document captures the first Stage 1 runtime tranche that moved the audio subsystem beyond singleton Stage 0 state into per-open stream behavior.

This tranche is intentionally conservative and does not claim hardware playback completion. It documents exactly what changed, how it behaves, what is still missing, and how to validate it manually in guest.

Primary related docs:

- docs/audio-subsystem-design.md
- docs/audio-subsystem-implementation-plan.md
- docs/audio-stage0-contract-pack.md

## Why this tranche exists

Stage 0 provided ABI and ioctl scaffolding, but stream behavior was effectively global and synthetic. Stage 1 requires stream objects keyed by file opens so multiple clients can coexist and so write path semantics can become Unix-like (blocking vs nonblocking, stream-local state, deterministic lifecycle).

This tranche lands the first half of that requirement:

1. Per-fd stream objects in kernel audio core.
2. Per-stream ring buffering in write path.
3. Open and close lifecycle hooks through VFS file paths.
4. Default node policy for audio endpoints through devman.

## Code paths changed

Kernel:

- kernel/audio/audio_core.c
- kernel/core/sysfile.c
- kernel/fs/file.c
- include/defs.h

Userland tooling and node policy:

- user/devman.c

Documentation:

- docs/ROADMAP.md
- docs/audio-subsystem-implementation-plan.md
- docs/audio-stage1-tranche1-runtime.md (this document)

## New runtime model

## Stream identity

Streams are now keyed by the opened file object (struct file pointer) for audio playback endpoints.

Important implications:

1. Multiple opens of /dev/pcmC0D0p map to distinct stream objects.
2. Stream lifecycle is bound to open and close, not global singleton state.
3. ioctl and write paths can operate on per-stream state.

## Stream object contents

Each stream currently tracks:

1. Ownership and mode:
- owner file pointer
- nonblock flag
- minor number

2. Parameters and controls:
- audio_stream_params
- audio_stream_volume
- stream_state

3. Runtime counters and pointers:
- hw_ptr_bytes
- sw_ptr_bytes
- queued_frames
- xruns/late_wakeups/period_misses/recoveries

4. Ring storage:
- ring base pointer
- ring_size
- ring_head/ring_tail

## Ring allocation

Ring backing is allocated per stream on open and released on close.

Current implementation uses one page-sized ring per stream.

## Lifecycle wiring

## Open path

sys_open now calls audio_open for AUDIODEV nodes.

Behavior:

1. Control minor (/dev/audioctl) is accepted without stream allocation.
2. Stream minors allocate per-fd stream object and ring.
3. O_NONBLOCK at open is captured into stream nonblock policy.

## Close path

fileclose now calls audio_close for AUDIODEV inode files.

Behavior:

1. Stream object is located by owner file pointer.
2. Waiters are woken.
3. Stream memory is cleared and ring memory is freed.

## Write path semantics

## VFS dispatch

filewrite now routes AUDIODEV inode writes to audio_filewrite (fd-aware) instead of direct inode writei path.

## Blocking behavior

When ring space is available, writes copy into ring and return copied bytes.

When ring is full:

1. Nonblocking stream returns failure when nothing was copied.
2. Blocking stream sleeps on ring-head wake channel and retries.

## Nonblocking behavior

Current behavior follows existing auxv6 convention where most code paths return -1 rather than Linux errno values.

So for nonblocking audio writes:

- if no data copied and ring full: return -1
- if partial data copied before full: return partial byte count

Future tranche can tighten this toward explicit EAGAIN-style reporting if desired by broader syscall policy.

## Consumption model

This tranche still uses null-backend style software consumption driven by elapsed ticks.

At write and status observation points, stream consumption advances according to:

- elapsed ticks
- stream sample_rate
- frame size

This keeps queue movement realistic enough for Stage 1 behavior validation without DMA or IRQ backend commitments.

## Ioctl behavior in this tranche

Stream-oriented ioctls now resolve through per-fd stream object for stream minors.

Per-stream handling includes:

- SET_PARAMS and GET_PARAMS
- PREPARE, START, STOP, DRAIN, DROP
- GET_STATUS
- SET_STREAM_VOL and GET_STREAM_VOL
- RESET_XRUN

Control-oriented ioctls remain endpoint-global and continue to use audioctl semantics:

- QUERY_ABI
- ENUM_DEVICES
- QUERY_CAPS
- SET_DEFAULT and GET_DEFAULT

## Procfs visibility

/ proc / audio summary now reports active stream count in addition to prior fields, so Stage 1 runtime usage can be observed.

(Actual path is /proc/audio. Written here with spacing only to avoid accidental markdown autolink artifacts in some viewers.)

## Dev node policy updates

devman now creates and cleans these defaults:

1. /dev/audioctl (major AUDIODEV, minor 0)
2. /dev/pcmC0D0p (major AUDIODEV, minor 1)

This removes manual node setup from normal bring-up and keeps node lifecycle in policy tooling.

## Locking and concurrency notes

The current tranche keeps one central audio core lock and uses sleep/wakeup channels around ring head movement.

Key properties:

1. Stream object mutation occurs under audio core lock.
2. Blocking writer waits on per-stream ring-head channel.
3. Close path wakes waiters before teardown.

Remaining risk areas for follow-up:

1. Clear lock-order documentation for future backend callback integration.
2. Poll/select readiness wake ordering once readiness path is wired.
3. Ensuring no stale stream references survive close race windows under heavy churn.

## Known limitations after tranche 1

1. poll/select readiness for audio fds is not wired yet.
2. Nonblocking semantics use current auxv6 -1 convention, not explicit errno values.
3. Null software consume model is not a hardware DMA backend.
4. /dev/pcmC0D0c capture path is still not active.
5. audiod policy daemon stage is still pending.

## Manual smoke sequence

Run inside guest after boot:

1. devman -s
2. ls -l /dev/audioctl /dev/pcmC0D0p
3. audioctl enum
4. audioctl set-params 48000 2 0 256 4 1024 /dev/pcmC0D0p
5. audioctl prepare /dev/pcmC0D0p
6. audioctl start /dev/pcmC0D0p
7. audiotest /dev/pcmC0D0p 64 512
8. audioctl status /dev/pcmC0D0p
9. cat /proc/audio
10. cat /proc/audio_stats

Expected high-level signals:

1. nodes exist and open succeeds
2. writes complete without panic
3. status shows queue and pointer movement
4. proc counters and active stream count are coherent

## Stage mapping

This tranche partially satisfies Stage 1 tasks in docs/audio-subsystem-implementation-plan.md:

1. Stream manager: started (per-fd allocation and lifecycle in place).
2. Buffering model: started (ring buffering plus blocking/nonblocking write behavior).
3. Stream ioctls: started (major stream ioctls mapped to per-stream state).
4. Readiness integration: not done yet.
5. Null backend cadence: partially done via tick-driven software consume.

## Next Stage 1 tranche recommendation

Priority order:

1. Add poll/select writable readiness and state-change readiness for audio fds.
2. Add stronger stream-state transition error accounting and xrun trigger semantics.
3. Add focused stress utility mode for multi-fd stream churn and close/write races.
4. Prepare for audiod Stage 2 by documenting stream control expectations and invariants.

## Change-control note

Any behavior change to stream lifecycle, status fields, or write semantics should update:

1. docs/audio-subsystem-implementation-plan.md
2. docs/audio-subsystem-design.md
3. docs/ROADMAP.md
4. this runtime note if Stage 1 internals changed
