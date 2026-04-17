# Audio Subsystem Implementation Plan (OSS-First Compatibility)

## Purpose

This document turns the architecture in `docs/audio-subsystem-design.md` into an execution plan with detailed, staged implementation work. It reflects the current project priority order:

1. Build a stable native audio subsystem.
2. Make OSS compatibility first class.
3. Extend compatibility to PulseAudio/sndio/ALSA-facing software.

The design inspirations remain:

- PulseAudio (userspace policy/mixing/routing).
- CoreAudio (timing discipline, hardware abstraction, robust stream lifecycle).

---

## Priority and Scope Policy

## Priority order

1. Native PCM core stability and observability.
2. First real playback backend stability (AC97).
3. OSS compatibility as first-class portability surface.
4. PulseAudio/sndio compatibility shims.
5. ALSA userspace compatibility plugin path.
6. Additional hardware backends and latency/perf tuning iterations.

## Scope rules

- Preserve native ABI as source-of-truth.
- Prefer userspace compatibility layers before kernel ABI cloning.
- Add kernel compatibility entrypoints only when real workloads require them.
- Keep implementation incremental and test-gated per phase.

---

## Baseline Assumptions

1. Existing kernel facilities are available: PCI, DMA helpers, IRQ registration, char devices, poll/select, procfs.
2. Threads are not yet a hard dependency for initial `audiod`; single-thread poll loop is acceptable in early phases.
3. `mmap` is not required for initial functionality but is a known future requirement for broader ALSA/pro-audio parity.
4. ext2-root/default system policy remains unchanged.

---

## Architecture Lock-in (What Must Not Drift)

## Fixed subsystem shape

- Kernel provides mechanism: stream state machine, hardware timing, DMA and wakeups.
- Userspace daemon (`audiod`) provides policy: mix, route, volume policy, defaults.
- Native interfaces:
  - `/dev/audioctl`
  - `/dev/pcmC*D*p` and `/dev/pcmC*D*c`
  - native audio ioctl family (`AUDIO_IOC_*`)
  - procfs telemetry endpoints

## Compatibility layering strategy

- OSS `/dev/dsp` and `/dev/mixer` compatibility implemented first.
- PulseAudio and sndio exposed via userspace shims over native path.
- ALSA exposed via userspace plugin/shim before considering large kernel emulation.

---

## Stage Plan (Detailed)

## Stage 0.5: PCI Chipset Discovery Stubs (Landed)

Goal:

- Detect historically common PCI audio controller families early and register
  them into the native audio core so later backend bring-up and OSS mapping
  can target real discovered hardware classes.

Implementation tasks:

1. Add a probe-stub tranche in `kernel/driver/audio_pci.c`.
2. Match fifteen common families across AC97, HDA, and legacy PCI audio.
3. Keep each family in its own driver source file (network-driver style),
  with `audio_pci.c` as orchestrator and shared probe/attach helpers in
  `kernel/driver/audio_pci_common.c`.
4. Register matched controllers with conservative per-profile caps via the
   audio core registry.
5. Tag registered devices with OSS-intent capability flags to keep
   `/dev/dsp` and `/dev/mixer` planning coupled to the native inventory path.

Definition of done:

- Kernel builds clean with audio PCI probe stubs enabled.
- `AUDIO_IOC_ENUM_DEVICES` returns discovered devices instead of a single
  fixed synthetic entry when matching controllers are present.
- `AUDIO_IOC_QUERY_CAPS` returns profile-based capability envelopes (AC97/HDA/
  legacy PCI) for requested or default routes.

Risks:

- Probe-only classification may over-approximate real codec limits; real
  backends must tighten caps once hardware programming paths exist.

## Stage 0: Contract Freeze and Skeleton

Goal:

- Freeze v1 native contract and create compile-clean skeletons for all major components.
- Stage 0 contract source-of-truth is `docs/audio-stage0-contract-pack.md`.

Implementation tasks:

1. Header/API definition
- Add `include/audio.h` core types:
  - card/device identifiers
  - stream direction/type enums
  - sample format enums
  - stream state enum
  - status/counter structs
- Add `include/audio_ioctl.h` ioctl request numbers and payload structs.
- Add explicit ABI versioning and reserved fields in all public structs.

2. Kernel source scaffolding
- Add `kernel/audio/audio_core.c`
- Add `kernel/audio/audio_stream.c`
- Add `kernel/audio/audio_procfs.c`
- Add `kernel/audio/audio_null.c` (null sink backend)

3. Build and declarations
- Update kernel build object lists.
- Add prototypes to `include/defs.h` where required.
- Add debug gate placeholders in line with existing debug policy docs.

4. Device-node policy scaffolding
- Define planned node naming policy for devman integration:
  - `/dev/audioctl`
  - `/dev/pcmC0D0p` / `/dev/pcmC0D0c`
  - reserved compatibility names for later (`/dev/dsp`, `/dev/mixer`)

Definition of done:

- Tree builds cleanly with stubbed audio subsystem enabled.
- `audioctl` ioctls compile and return deterministic `ENOSYS`/`EOPNOTSUPP` style results for unimplemented ops.
- Procfs stubs are visible with clearly marked placeholder values.

Risks:

- ABI churn if versioning/padding is skipped.

Exit checks:

- `make aux.kern` build-clean.
- Header audit for fixed-size fields and no accidental pointer-in-ABI exposure.
- Contract-pack checklist reviewed and accepted from `docs/audio-stage0-contract-pack.md`.

---

## Stage 1: Native PCM Core (Playback Path)

Goal:

- Implement native playback stream lifecycle and control path over null backend.

Status (2026-04-05):

- In progress (Tranche 1 landed): per-fd stream objects keyed by open file,
  per-stream software ring buffers, blocking/nonblocking write behavior on
  the native PCM endpoint, and stream lifecycle open/close wiring in kernel
  file paths. Default `/dev/audioctl` and `/dev/pcmC0D0p` node creation was
  added to `devman` so Stage-1 flows are available by policy.
- Detailed runtime behavior and known limits are tracked in
  `docs/audio-stage1-tranche1-runtime.md`.
- Poll/select readiness integration details are tracked in
  `docs/audio-stage1-tranche2-readiness.md`.
- Per-stream procfs observability details are tracked in
  `docs/audio-stage1-tranche3-observability.md`.
- Runtime `F_SETFL`/`F_GETFL` nonblocking integration is now wired for
  audio stream descriptors.

Implementation tasks:

1. Stream manager
- Create per-stream objects keyed by fd open context.
- Implement state machine:
  - `NEW -> CONFIGURED -> PREPARED -> RUNNING -> XRUN/STOPPED -> DRAINED/CLOSED`
- Enforce legal transitions and deterministic error returns.

2. Buffering model
- Implement software ring buffer for playback stream.
- Support blocking and nonblocking write semantics.
- Expose `hw_ptr` and `sw_ptr` accounting in status query.

3. Control ioctls (native)
- Implement at minimum:
  - `AUDIO_IOC_QUERY_CAPS`
  - `AUDIO_IOC_ENUM_DEVICES`
  - `AUDIO_IOC_SET_PARAMS`
  - `AUDIO_IOC_GET_PARAMS`
  - `AUDIO_IOC_PREPARE`
  - `AUDIO_IOC_START`
  - `AUDIO_IOC_STOP`
  - `AUDIO_IOC_DRAIN`
  - `AUDIO_IOC_GET_STATUS`

4. Readiness integration
- Wire poll/select readiness events for write-space and state change wakeups.

5. Null backend execution path
- Consume stream data at configured period cadence.
- Track underruns/period misses.

6. Stream observability
- Expose active stream table via `/proc/audio_clients`.
- Keep `audiostat` output aligned with `/proc/audio*` surfaces.

Definition of done:

- Multiple clients can open/play/stop/drain against null backend.
- No panic, deadlock, or leaked stream objects in stress loops.
- Status and counters behave monotonically and match exercised behavior.

Risks:

- Lock-order mistakes between stream lock, wakeup paths, and global registry lock.

Exit checks:

- `audiotest -n` null sink stress passes repeatedly.
- `audiotest -x` xrun injection/recovery path passes.

---

## Stage 2: `audiod` Minimal Policy Daemon

Goal:

- Provide first userspace policy/mixing layer over native playback.

Status (2026-04-05):

- Started (Tranche 1 landed): `audiod` now has a minimal daemon lifecycle
  and single-sink poll loop that configures one native PCM playback stream,
  writes silence on writable readiness, and performs xrun recovery.
- Tranche 2 landed: `audiod` now accepts one-shot local control commands
  (status, stream set-params reconfigure, write size, poll timeout) and
  `audiodctl` can submit those commands via a mailbox file path.
- Detailed behavior and limits are tracked in
  `docs/audio-stage2-tranche1-daemon-scaffold.md`.
- Control-path behavior is tracked in
  `docs/audio-stage2-tranche2-control-path.md`.

Implementation tasks:

1. Daemon lifecycle
- Implement `user/audiod.c` single-thread event loop using poll/select.
- Manage client connection/control channels.

2. Mixer path
- Implement fixed-point software mixing (S16 first, S32 follow-up).
- Add per-stream gain and master gain.
- Clip accounting.

3. Stream routing policy (minimal)
- One default sink.
- Last-used sink persistence in `/etc/audio.conf`.

4. Client library baseline
- Implement `libaudio` basics:
  - connect/disconnect
  - open/write/drain/close stream
  - set volume

5. CLI tooling (baseline)
- `audioctl`: enumerate, inspect status, set default sink/master volume.
- `audiostat`: display `/proc/audio*` counters.

Definition of done:

- 2+ clients can play simultaneously through audiod mixing path.
- audiod restart has controlled failure behavior and recovers cleanly for new streams.

Risks:

- CPU cost in mixer loop causing avoidable xruns.

Exit checks:

- Mixed-stream soak test with varied write chunk sizes.
- Counter sanity: xruns correlate with induced stress and recover operations.

---

## Stage 3: First Hardware Backend (AC97)

Goal:

- Deliver real audible playback in QEMU-targeted configuration.

Implementation tasks:

1. Driver implementation (`kernel/driver/ac97.c` or equivalent)
- PCI probe/attach.
- DMA ring/buffer setup.
- Start/stop/prepare/drain operations.
- IRQ completion (with safe polling fallback if needed).

2. HAL binding
- Register AC97 backend into audio core with capability table.
- Support baseline format target: 48kHz, stereo, S16_LE.

3. Error and reset behavior
- Define deterministic failure codes for unsupported params.
- Implement robust stop/reset path.

4. User utility
- Implement `audioplay` WAV/PCM smoke utility.

Definition of done:

- Reliable playback in configured QEMU AC97 path.
- Stable repeated start/stop/drain cycles.
- Bounded recovery on forced underrun.

Risks:

- IRQ path instability or DMA teardown race during stop/reset.

Exit checks:

- Manual QEMU playback validation by operator.
- No kernel panic in repeated playback loops.

---

## Stage 4: Native Stability Hardening

Goal:

- Freeze native behavior before compatibility expansion.

Implementation tasks:

1. Observability expansion
- Finalize `/proc/audio`, `/proc/audio_stats`.
- Optional `/proc/audio_clients` for pid/format/state visibility.

2. Error model hardening
- Codify xrun behavior and reprepare requirements.
- Ensure all ioctls validate struct sizes/versions/padding.

3. Performance guardrails
- Bound hot-path allocations.
- Add period miss and wake-late counters.

4. Regression tooling
- Extend `audiotest` profiles:
  - long-run stream churn
  - mixed blocking/nonblocking writes
  - randomized buffer/period params in supported ranges

Definition of done:

- Native API considered stable for compatibility mapping.
- No recurring crash/deadlock class from stress pass.

Risks:

- Premature compatibility layering before native behavior is deterministic.

Exit checks:

- Multi-run stability matrix on null + AC97 backends.

---

## Stage 5: OSS First-Class Compatibility (Primary Post-Stability Goal)

Goal:

- Make OSS the first production compatibility surface.

Implementation tasks:

1. OSS device endpoints
- Add `/dev/dsp` compatibility playback endpoint.
- Add `/dev/mixer` baseline control endpoint.

2. OSS ioctl translation layer
- Implement common OSS calls expected by portable software:
  - format negotiation
  - channel count
  - sample rate
  - block fragment/buffer sizing semantics where practical
  - mixer volume controls
- Map to native `AUDIO_IOC_*` behavior with deterministic compatibility semantics.

3. Behavior contract doc
- Add explicit supported OSS ioctl matrix and fallback behavior.
- Document unsupported calls and exact return/error policy.

4. Test coverage
- Add `audiotest --oss` path for open/configure/write/drain/close loops.
- Add sample OSS-style smoke utility if needed.

Definition of done:

- Representative OSS-oriented ports run with no source changes or minimal build toggles.
- OSS path remains stable when switching null <-> AC97 backend.

Risks:

- Implicit OSS semantics mismatch around fragment and blocking behavior.

Exit checks:

- OSS smoke and stress matrix clean.
- Manual playback confirmation via OSS path.

---

## Stage 6: PulseAudio and sndio Compatibility

Goal:

- Unlock broader Linux/BSD portable software after OSS baseline is solid.

Implementation tasks:

1. PulseAudio compatibility (userspace first)
- Implement practical `libpulse-simple`-grade shim over audiod.
- Expand toward needed `libpulse` patterns based on port demand.

2. sndio compatibility shim
- Implement `libsndio`-style subset over audiod/native stream controls.

3. Behavior alignment
- Match timing/blocking/drain expectations closely enough for real applications.
- Document known divergence.

Definition of done:

- At least one representative app per compatibility family runs successfully.

Risks:

- Protocol/semantic edge cases around latency reporting and drain completion.

Exit checks:

- Port-level smoke list (defined and reproducible).

---

## Stage 7: ALSA-Facing Compatibility (Userspace Plugin First)

Goal:

- Support common ALSA-using software without full kernel ALSA ABI emulation.

Implementation tasks:

1. ALSA userspace backend plugin
- Map common ALSA PCM operations to audiod/native interfaces.
- Implement basic hw/sw params translation.

2. Non-mmap path first
- Ensure write-based playback works reliably.
- Defer mmap-specific parity until kernel mapping capabilities justify it.

3. Gap analysis
- Track which ALSA behaviors require additional native features.

Definition of done:

- Common ALSA client software can play audio through plugin path.

Risks:

- Apps requiring strict mmap/ring semantics may need further kernel work.

Exit checks:

- Defined ALSA app smoke list with pass/fail notes.

---

## Stage 8: Backend Expansion and Long-Term Hardening

Goal:

- Broaden hardware support while preserving API and compatibility stability.

Implementation tasks:

1. HDA backend bring-up.
2. virtio-snd backend bring-up.
3. Backend capability normalization in core.
4. Latency/performance tuning iterations.

Definition of done:

- Backend switch does not break native API or compatibility layers.

---

## Cross-Cutting Workstreams

## A) ABI governance

- Maintain explicit ABI version constant.
- Add compatibility test for struct-size and field-offset assumptions.
- Require doc updates for every ABI-affecting change.

## B) Locking and concurrency hygiene

- Keep lock ordering documented for audio core and backend callback paths.
- Add debug logging gates for lock/stream transitions in debug builds only.

## C) Observability

- Ensure counters are low-cost and useful under stress.
- Avoid verbose logging in hot path by default.

## D) Documentation discipline

For every completed stage:

1. Update `docs/audio-subsystem-design.md` status notes.
2. Update `docs/ROADMAP.md` stage progress.
3. Add/refresh man pages for user-visible tools.
4. Keep compatibility matrix current.

---

## Compatibility Matrix (Target State)

## Native

- `/dev/audioctl` + `/dev/pcm*`: first-class, canonical.

## OSS

- `/dev/dsp`, `/dev/mixer`: first-class post-stability compatibility target.

## PulseAudio

- Userspace shim, practical subset first, expanded by demand.

## sndio

- Userspace shim subset for BSD-oriented applications.

## ALSA

- Userspace plugin/shim path first; kernel ABI emulation only if proven necessary.

---

## Milestone Gate Checklist

A stage can be marked complete only if all are true:

1. Build-clean (`make aux.kern` and relevant userland targets).
2. Stage-specific smoke/stress tests pass.
3. No new unresolved panic/deadlock regressions in stage scope.
4. Procfs and diagnostics remain coherent.
5. Documentation and man-page updates are landed for user-visible changes.

---

## Initial Execution Order (Actionable)

1. Stage 0 contract freeze and scaffolding.
2. Stage 1 native playback core on null backend.
3. Stage 2 `audiod` minimal mixing policy.
4. Stage 3 AC97 real playback path.
5. Stage 4 native stability hardening.
6. Stage 5 OSS first-class compatibility.
7. Stage 6 PulseAudio/sndio shims.
8. Stage 7 ALSA userspace plugin path.
9. Stage 8 backend expansion (HDA, virtio-snd).

This order is the implementation policy unless changed by an explicit roadmap decision.
