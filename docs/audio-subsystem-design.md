# Audio Subsystem Architecture for auxv6

## Executive Summary

This document proposes a Unix-like audio subsystem for auxv6 inspired by:

- PulseAudio: userspace policy daemon, per-client streams, software mixing, and per-stream volume/routing.
- CoreAudio: strict hardware abstraction (HAL), timing-first engine design, low-latency callback discipline, and explicit format conversion stages.

The auxv6 design is a hybrid:

- Kernel owns timing-critical data movement and device DMA.
- Userspace owns policy: session defaults, per-app volume, mixing policy, stream routing, and persistence.

This keeps the kernel small and deterministic while enabling modern multi-application audio behavior.

## Implementation Status Snapshot (2026-04-05)

- Stage 0 contract and ABI surfaces are landed (headers, ioctls, procfs, basic user tools).
- Stage 1 has started with per-fd stream objects, per-stream software ring buffering, and stream lifecycle open/close hooks in kernel file paths.
- Stage 1 readiness integration has started: poll/select now receives AUDIODEV-specific writable/error readiness derived from stream ring space and xrun state.
- Stage 1 observability expanded with `/proc/audio_clients` to expose active stream slot/state/queue snapshots.
- Audio stream descriptors now honor runtime `O_NONBLOCK` toggles through `fcntl(F_SETFL)` and report state via `F_GETFL`.
- Default audio endpoint node policy is now wired in devman for `/dev/audioctl` and `/dev/pcmC0D0p`.
- Stage 2 has started with a minimal `audiod` daemon scaffold that configures one native playback stream and services it from a poll-driven loop with xrun recovery.
- Stage 2 control-path follow-on landed: `audiod` now accepts one-shot runtime commands via a local mailbox file, and `audiodctl` can trigger status and live reconfigure changes.
- Detailed Stage-1 tranche-1 runtime behavior is documented in `docs/audio-stage1-tranche1-runtime.md`.
- Detailed Stage-1 tranche-2 readiness behavior is documented in `docs/audio-stage1-tranche2-readiness.md`.
- Detailed Stage-1 tranche-3 observability behavior is documented in `docs/audio-stage1-tranche3-observability.md`.
- Detailed Stage-2 tranche-1 daemon behavior is documented in `docs/audio-stage2-tranche1-daemon-scaffold.md`.
- Detailed Stage-2 tranche-2 control behavior is documented in `docs/audio-stage2-tranche2-control-path.md`.

---

## Goals

1. Provide reliable PCM playback first, then capture.
2. Support multiple concurrent clients without requiring every app to implement a mixer.
3. Keep deterministic timing and predictable underrun behavior.
4. Preserve simple direct-device mode for diagnostics and low-overhead workloads.
5. Fit existing auxv6 conventions: VFS-style devices, devman node creation, poll/select readiness, procfs observability, and incremental driver bring-up.
6. Make OSS compatibility a first-class portability target once the native subsystem is stable.
7. Provide practical compatibility paths for PulseAudio-style, sndio-style, and ALSA-using software without freezing early kernel design around one foreign ABI.

## Non-goals (Initial Tranche)

1. No full JACK-style graph scheduler in v1.
2. No compressed codec offload in kernel.
3. No Bluetooth audio in initial implementation.
4. No hard real-time guarantees; target low-jitter best effort under current scheduler model.

---

## Design Principles

### 1) Split mechanism and policy

- Kernel mechanism: clock, ring buffers, DMA kick/completion, wakeups, hardware safety limits.
- Userspace policy: stream lifecycle, software mixing, resampling, routing, role-based defaults, ducking rules.

### 2) One stable kernel PCM ABI

All drivers implement a common PCM backend interface. User-visible ABI is device-agnostic.

### 3) Pull the complexity up

Like PulseAudio, shared mixing and policy live in a daemon (`audiod`) not in every application.

### 4) Timing discipline from CoreAudio

The engine is period-based and clocked; every stream state transition is measured against monotonic time.

### 5) Incremental delivery with a null backend first

A null sink backend allows full stream/mixer validation before hardware driver completion.

---

## Architecture Overview

```
+--------------------------------------------------------------+
| Applications (player, shell tools, games, server7 clients)  |
+---------------------+------------------+---------------------+
| libaudio client API | audioctl utility | test/stress tools    |
+---------------------+------------------+---------------------+
| audiod (userspace policy daemon, mixer, routing, resampler) |
+---------------------+------------------+---------------------+
| /dev/audioctl + /dev/pcm* char devices + ioctl + poll       |
+--------------------------------------------------------------+
| Kernel audio core: stream manager, timing engine, HAL bridge |
+--------------------------------------------------------------+
| Driver backends: null, AC97, Intel HDA, virtio-snd (later)   |
+--------------------------------------------------------------+
| PCI + DMA + IRQ infrastructure                               |
+--------------------------------------------------------------+
```

---

## Kernel Components

## 1. Audio Core (`kernel/audio/`)

Responsibilities:

- Global audio registry and card discovery.
- Stream object lifecycle.
- Common period timer and wakeup path.
- Format validation and constraints negotiation.
- XRUN detection (underrun/overrun) and accounting.
- Procfs stats export.

Proposed files:

- `kernel/audio/audio_core.c`
- `kernel/audio/audio_stream.c`
- `kernel/audio/audio_mixer.c` (minimal utility paths only)
- `kernel/audio/audio_procfs.c`
- `include/audio.h`
- `include/audio_ioctl.h`

## 2. PCM HAL Interface

Each driver implements:

- `open_stream`, `close_stream`
- `hw_params`, `prepare`, `start`, `stop`, `drain`
- `pointer` (current DMA hw position)
- `ack_period` (optional explicit period acknowledgement)
- `set_gain` (optional hardware gain)

The core never depends on hardware-specific register layout.

## 3. Character Devices

Device nodes:

- `/dev/audioctl` global policy/control endpoint.
- `/dev/pcmC0D0p` playback endpoint (card0 device0 playback).
- `/dev/pcmC0D0c` capture endpoint (later tranche).

Primary operations:

- `open/close`
- `read/write` (for simple clients)
- `ioctl` for params and transport control
- `poll/select` for readiness

## 4. IOCTL ABI (v1)

Control path modeled after practical ALSA-style subsets while staying auxv6-native.

- `AUDIO_IOC_QUERY_CAPS`
- `AUDIO_IOC_ENUM_DEVICES`
- `AUDIO_IOC_SET_PARAMS` (rate, channels, sample format, period, buffer)
- `AUDIO_IOC_GET_PARAMS`
- `AUDIO_IOC_PREPARE`
- `AUDIO_IOC_START`
- `AUDIO_IOC_STOP`
- `AUDIO_IOC_DRAIN`
- `AUDIO_IOC_GET_STATUS` (state, delay, xruns, hw_ptr, sw_ptr)
- `AUDIO_IOC_SET_STREAM_VOL`
- `AUDIO_IOC_GET_STREAM_VOL`

ABI notes:

- Use fixed-size integer fields and explicit reserved padding.
- Include `abi_version` in all top-level control structs.
- Keep forward-compatible extensibility by size-tagged payloads.

## 5. Timing Model

- Period-based engine with monotonic clock timestamps.
- Target v1 defaults: 48 kHz, 2 channels, S16_LE, 256-frame periods, 4 periods per buffer.
- Wake clients/daemon on period boundary and threshold changes.

Default latency model:

- Default path: approximately 20 to 30 ms end-to-end for stability.
- Low-latency mode (opt-in): approximately 10 to 15 ms where scheduler headroom permits.

## 6. Kernel Observability

Add procfs endpoints:

- `/proc/audio` summary: devices, active streams, default format.
- `/proc/audio_stats` counters: xruns, late wakeups, period misses, recovery count.
- `/proc/audio_clients` optional stream table (pid, state, format, latency target).

---

## Userspace Components

## 1. `audiod` daemon

Primary roles (PulseAudio-inspired policy layer):

- Accept client stream connections.
- Maintain per-stream ring state and volume.
- Perform software mixing to device format.
- Perform sample rate conversion when stream format mismatches device format.
- Apply routing/default-device policy.
- Recover from driver restart or temporary XRUN storms.

Design constraints for auxv6:

- Single-thread event loop first (poll/select based).
- Worker split is optional future work after thread groundwork lands.

## 2. `libaudio`

Client-facing API for apps:

- `audio_connect`, `audio_disconnect`
- `audio_stream_open`, `audio_stream_write`, `audio_stream_drain`, `audio_stream_close`
- `audio_stream_set_volume`, `audio_stream_get_latency`

v1 transport:

- Control via `ioctl` and stream fd operations.
- Data via `write` path for compatibility.
- Optional zero-copy shared-buffer transport is a future tranche after wider mapping support is stable.

## 3. CLI tools

- `audioctl`: enumerate cards/devices, set default sink/source, set volume/mute, inspect stream graph.
- `audioplay`: raw PCM and WAV playback smoke utility.
- `audiostat`: live counters from procfs.
- `audiotest`: deterministic tone/sweep/xrun stress utility.

All tools should have man pages and build/clean integration.

---

## Driver Strategy

## Bring-up order

1. Null backend (`snd_null`): functional framework and test coverage with no hardware.
2. AC97 PCI backend: simplest broadly emulatable path in QEMU for first real output.
3. Intel HDA backend: modern baseline and better long-term parity.
4. virtio-snd backend: efficient VM-focused path once core is stable.

## Driver requirements

Each backend must provide:

- Format/rate/channel capability table.
- DMA ring setup and teardown.
- IRQ completion or equivalent polling fallback.
- Robust stop/reset path with no stuck DMA.
- Deterministic error reporting on unsupported operations.

---

## Compatibility Strategy (Post-Stability)

Compatibility becomes the primary goal after the native core and first hardware backend are stable.

Interface priorities:

1. OSS first-class support (mandatory first compatibility tranche).
2. PulseAudio client compatibility (practical subset first).
3. sndio compatibility surface for BSD-oriented ports.
4. ALSA compatibility path via userspace plugin/shim before considering any full kernel-ABI clone.

Proposed compatibility surfaces:

- OSS: `/dev/dsp` and `/dev/mixer` compatibility layer mapped onto native PCM/audiod policy.
- PulseAudio: `libpulse-simple`-grade shim first, then broader `libpulse` behavior as needed.
- sndio: lightweight `libsndio`-style shim over audiod/native PCM controls.
- ALSA: `alsa-lib` backend plugin mapping to audiod/native interfaces; avoid committing early to large kernel ALSA ioctl breadth.

Compatibility guardrails:

- Keep native audio ABI as the source of truth.
- Add compatibility layers in userspace where feasible.
- Only add kernel ABI emulation when a real workload cannot be supported cleanly from userspace.

---

## Stream and Mixing Model

## Stream states

`NEW -> CONFIGURED -> PREPARED -> RUNNING -> XRUN/STOPPED -> DRAINED/CLOSED`

## Mixing model (v1)

- Mix in userspace daemon into one hardware playback stream per sink.
- Fixed-point mixer first (S16/S32) for deterministic behavior and no FPU dependency assumptions.
- Soft limiter optional; clipping counter always exposed.

## Volume model

- Per-stream software gain in daemon.
- Optional per-device hardware gain if backend supports it.
- Effective gain = stream_gain * sink_gain * master_gain.

---

## Session and Policy Model

PulseAudio-inspired policy rules for v1:

- Per-session default sink selection.
- Last-used sink persistence in `/etc/audio.conf` plus runtime overrides.
- Basic role hints (`music`, `voice`, `system`) with simple ducking policy deferred to v2.

CoreAudio-inspired operational safeguards:

- Device clock is authoritative.
- Stream timeline correction based on measured drift between software pointer and hardware pointer.
- Rate matching via resampler, not by violating period cadence.

---

## Security and Isolation

- Restrict direct hardware PCM open to root or `audio` group policy.
- Normal apps use daemon-managed shared path by default.
- Validate all user-provided audio params in kernel (size, format, bounds).
- Harden against malformed ioctl payloads and partial writes.

---

## Error Model and Recovery

- XRUN transitions stream to recoverable state, not immediate process kill.
- `AUDIO_IOC_PREPARE` can rearm stream after XRUN.
- Daemon retries device start with bounded backoff and logs reason codes.
- If backend fails hard, daemon can switch to null sink and keep clients alive with explicit degraded status.

---

## Build and Source Layout Proposal

- `kernel/audio/` for core.
- `kernel/driver/ac97.c`, `kernel/driver/hda.c`, `kernel/driver/virtio_snd.c`.
- `include/audio.h`, `include/audio_ioctl.h`.
- `user/audiod.c`, `user/audioctl.c`, `user/audioplay.c`, `user/audiostat.c`, `user/audiotest.c`.
- `targetfs/usr/share/man/audioctl.md` (and matching pages).
- `docs/audio-subsystem-design.md` (this document) plus follow-on status doc once implementation starts.

---

## Implementation Plan (Phased)

## Phase A: ABI and Null Backend

Deliverables:

- Kernel audio core scaffolding and ioctl ABI.
- `/dev/audioctl` and one null playback device.
- `audiod` minimal event loop with one sink.
- `audioctl` enumeration and status output.
- `/proc/audio` baseline stats.

Definition of done:

- Multiple client streams can be opened, mixed, and drained against null sink.
- No panic or deadlock under repeated open/write/stop loops.

## Phase B: Real Hardware Playback (AC97)

Deliverables:

- AC97 playback DMA path and IRQ handling.
- format negotiation (at least 48 kHz S16 stereo).
- `audioplay` WAV/PCM smoke tool.

Definition of done:

- Reliable playback in QEMU AC97 profile.
- Stable start/stop/drain cycles and bounded XRUN recovery.

## Phase C: Latency and Diagnostics

Deliverables:

- Period timing tuning and latency knobs.
- `/proc/audio_stats` detailed counters.
- `audiotest` stressor and `audiostat` live monitor.

Definition of done:

- Measurable, repeatable latency profiles with no runaway XRUN under nominal load.

## Phase D: Capture + Session Policy

Deliverables:

- Capture device path.
- policy for default source/sink and per-session behavior.
- `audioctl` volume/mute and default routing controls.

Definition of done:

- Full duplex smoke path (record and play).

## Phase E: HDA and virtio-snd expansion

Deliverables:

- Intel HDA backend.
- virtio-snd backend (if host support available).
- compatibility hardening across backends.

Definition of done:

- Core ABI unchanged across backend additions.
- Backend switch does not break userland API.

## Phase F: OSS First-Class Compatibility

Deliverables:

- `/dev/dsp` compatibility endpoint backed by native PCM streams.
- `/dev/mixer` compatibility endpoint for baseline mixer controls.
- OSS ioctl translation layer for common playback and volume flows.
- `audiotest` coverage for OSS open/configure/write/drain/close loops.

Definition of done:

- Representative OSS-using ports run without source changes or with minimal build-time toggles.
- OSS compatibility path remains stable across AC97/HDA backend switches.

## Phase G: Broader Portability Interfaces

Deliverables:

- PulseAudio compatibility shim (start with `libpulse-simple` behavior used by common ports).
- sndio compatibility shim for BSD software expecting `libsndio` semantics.
- ALSA userspace backend plugin that maps common PCM flows onto audiod/native PCM.

Definition of done:

- Portable Linux/BSD applications using at least one of PulseAudio, sndio, or ALSA userspace APIs can play audio without kernel ABI forks.

---

## Test Plan

Kernel/userland regression additions:

1. `audiotest -n`: null sink stress (open/write/close loops, random buffer sizes).
2. `audiotest -x`: XRUN injection and recovery verification.
3. `audiotest -p`: period-boundary timing drift checks.
4. `audioplay`: WAV playback correctness and drain behavior.
5. procfs assertions: counters monotonicity and stream-state transitions.

Manual validation targets:

- QEMU AC97 profile playback.
- long-run playback under mixed system load.
- daemon restart while clients are active (controlled failure behavior).

---

## Performance and Stability Guardrails

1. Keep mixer cost bounded: O(active_streams * frames_per_period).
2. Avoid unbounded allocation in period hot path.
3. Never hold broad global locks across device I/O callbacks.
4. Keep lock ordering explicit for audio core, backend, and process wakeup interactions.
5. Expose period miss counters to catch scheduler regressions early.

---

## Documentation Follow-through

When implementation begins, update:

- `docs/ROADMAP.md` subsystem tables and tranche status.
- man pages for new tools (`audioctl`, `audioplay`, `audiostat`, `audiotest`).
- any relevant debug logging gates in `docs/debug-logging.md`.

This design intentionally starts with a practical, testable baseline and leaves room for future compatibility layers (for example, ALSA-style plugin shims) without coupling those concerns into kernel v1.
