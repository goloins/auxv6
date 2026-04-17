# Audio Subsystem Audit Report
**Date**: April 16, 2026  
**Status**: Functional scaffold with single critical architectural blocker; 80% framework, 20% real backend

---

## Executive Summary

The auxv6 audio subsystem is **architecturally sound** (native ABI + userspace policy daemon) but **operationally limited** to single-stream playback. The implementation spans three deliverable stages (Stage 0: Contract, Stage 1: Per-FD Streams, Stage 2: Daemon Policy), with Stage 0–2 Tranches 1–3 complete and functional. AC97 backend provides the only real hardware DMA/IRQ path; 14 PCI audio device family stubs exist but are probe-only; capture is unimplemented; mixer controls are unwired; multi-stream concurrency is blocked by a simple but consequential code choice in the hardware handoff function.

**Critical Architectural Issue**: `audio_hw_period_advance()` in `audio_core.c` (lines 1701–1775) selects the **first running stream only** per period boundary. This was acceptable during scaffolding but is now a hard limit on scalability. Fixing it requires either explicit stream←→DMA binding (complex at kernel level) or moving all mixing to the daemon (preferred, already designed for).

---

## Completed Subsystems

### Stage 0: ABI Contract (Complete)
- **Files**: `include/audio.h`, `include/audio_ioctl.h`
- **Stream state machine**: NEW → CONFIGURED → PREPARED → RUNNING; XRUN/STOPPED → DRAINED/CLOSED
- **Format support**: S16_LE, U8, S32_LE (defined enums; hardware profiles expose subset)
- **Device node policy**: `/dev/audioctl` (global control), `/dev/pcmC0D0p/q` (stream nodes for playback/capture)
- **Native ioctls** (12 total): QUERY_ABI, ENUM_DEVICES, QUERY_CAPS, SET/GET_PARAMS, PREPARE, START, STOP, DRAIN, DROP, GET_STATUS, SET/GET_STREAM_VOL, RESET_XRUN
- **ABI version**: 1.0.0 (hardcoded; validation checks major version and struct sizing)

### Stage 1: Per-FD Stream Framework (Complete)
**File**: `kernel/audio/audio_core.c` (~1800 lines)
- **per-fd stream objects**: `struct audio_stream` keyed by file descriptor; each fd gets independent state machine, ring buffer, parameters, counters
- **Software ring buffering**: 4KB default per stream; wrap-aware head/tail pointers; AUDIO_RINGBUF_SIZE compile constant
- **Nonblocking semantics**: `open(O_NONBLOCK)` sets nonblock flag; blocking writes sleep on `&ticks` until space available or timeout
- **State transitions**: File-write calls `audio_stream_alloc_locked()` on first write to lazy-init stream; SET_PARAMS resets ring and transitions to CONFIGURED; PREPARE→PREPARED; START→RUNNING
- **Ioctl dispatch**: All 12 control ioctls fully implemented; SET_PARAMS/GET_PARAMS/GET_STATUS work with live parameters
- **Hardware readiness**: `audio_poll_events()` returns XRUN (error) or free-space (writable) events via poll(2)
- **Trivial consumption model**: Tick-based accounting (AUDIO_TICKS_PER_SEC=100) simulates hardware drain during write/status queries; not real hw_ptr query

### Stage 2: Daemon Policy Layer (Functional Scaffold)
**File**: `user/audiod.c` (~780 lines)
- **Poll loop**: Single-threaded event loop; opens `/dev/pcmC0D0p`, configures stream (SET_PARAMS → PREPARE → START), polls POLLOUT + mailbox fd
- **Silence write**: Periodic zero fills on underrun to keep stream alive; XRUN detected when ring exhausts; RESET_XRUN → PREPARE → START recovery
- **Mixing skeleton**: 8-track flat array with per-track gain_shift; S16_LE 2-channel accumulator loop with saturation clip-to-int16
- **Control mailbox**: `/tmp/audiod.ctl` accepts text commands: `status`, `set <r c f p n b>` (rate/chans/format/periods/frames/blocksize), `set-write <bytes>`, `set-timeout <ms>`
- **Limitations**: Mailbox is non-queued (last writer wins on race); mixing loop never tested at real client load; still single stream to hardware; no resampling or per-client volume

### Stage 1 Tranche 3: Observability (Complete)
**File**: `kernel/audio/audio_core.c`, `user/audiostat.c`
- **Procfs exports**: Three endpoints:
  - `/proc/audio` – summary (ABI version, device count, active streams)
  - `/proc/audio_stats` – per-ioctl counters, xrun count, late wakeup count
  - `/proc/audio_clients` – active stream table (fd, state, queue frames, hw_ptr, sw_ptr, xruns)
- **Simple CLI**: `audiostat` reads and pretty-prints `/proc/audio*`

### AC97 Hardware Backend (Real Implementation, Incomplete)
**File**: `kernel/driver/audio_intel_ac97.c` (~450 lines)
- **DMA infrastructure**: 2KB bounce buffer × 32 slots = 64KB total; Buffer Descriptor List (BDL) pre-allocated with period-boundary IOC interrupt enable
- **Period completion**: Triggered by AC97 BCIS (Buffer Completion Interrupt Status) or polled; hands 2048-byte block to `audio_hw_period_advance()` via ring copy + zero-pad on underrun
- **Codec init**: AC97 reset sequence, master volume set, supported rates/channels queried from codec (used to validate client params)
- **Interrupt + poll**: Registered via `request_irq()`; also polled from write path for low-latency service
- **Limitations**:
  - Only playback DMA wired; capture (PCM-In) registers untouched
  - No mixer control wiring (volume set but not exposed to AUDIO_IOC_SET_STREAM_VOL)
  - No real `pointer()` callback (hw_ptr remains synthesized from ticks, not read from CIV register)
  - Single-stream assumption (BDL not multiplexed per stream; all periods served from one running stream)

### PCI Device Discovery (Probe Stubs)
**File**: `kernel/driver/audio_pci.c` and 14 family stubs
- **Device matching**: 15 audio device families (AC97, HDA generic, Realtek HDA, Sigmatel HDA, Conexant HDA, VIA HDA, NVIDIA HDA, AMD HDA, Intel HDA, Broadcom HDA, Conexant CX, VIA, Legacy, Ice1712, Emu10k)
- **Probe pattern**: Vendor/device/class code matching, BAR selection heuristic (prefer MMIO ≥4KB), registration via `audio_register_hw_device()` with profile-based capability defaults
- **Current state**: AC97 fully implemented; all others are 15–40 line stubs that probe and register only (no DMA/IRQ wiring)
- **Capability profiles**: AC97→48kHz/1-2ch/S16_LE/U8; HDA families→48–192kHz/1-2ch/S16-32_LE

---

## Architectural Gaps

### 1. Single-Stream Hardware Binding (Critical Blocker)
**Location**: `audio_hw_period_advance()`, lines 1701–1775 of `kernel/audio/audio_core.c`

```c
/* Find the first active running stream. */
found = 0;
for(i = 0; i < AUDIO_STREAM_MAX; i++){
  if(!audio_streams[i].in_use)
    continue;
  if(audio_streams[i].stream_state != AUDIO_ST_RUNNING)
    continue;
  found = 1;
  s = &audio_streams[i];
  break;  // <-- STOPS AT FIRST; NO LOOP OVER ALL RUNNING STREAMS
}
```

**Impact**: Hardware can serve only one client at a time. If two `audioctl` processes write simultaneously, only the first-found stream gets the current DMA period; the other starves.

**Solution**: Either (a) explicit stream←→DMA slot binding (kernel-side, complex), or (b) daemon-side mixing (recommended; daemon becomes policy layer that accepts multiple clients, mixes to one hardware stream, applies per-client volume).

### 2. No Formal Hardware Abstraction Layer
**Missing**: `struct audio_hw_ops` (equivalent to NetBSD `audio_hw_if` or ALSA `snd_pcm_ops`)

**Current state**: Core directly calls AC97 functions; no generic driver contract. New backends must reverse-engineer the pattern from AC97.

**Impact**: HDA, Realtek, and other stubs cannot be completed without ad-hoc coding per family. No way to query hardware capabilities at registration time (still hardcoded profiles).

**Comparison**:
- **NetBSD**: `audio_hw_if` struct with callbacks: `query_format()`, `set_format()`, `init_output()`, `trigger_output()`, `halt_output()`, `pointer()`, mixer ops
- **ALSA**: PCM operators (`snd_pcm_ops`) with `open()`, `close()`, `hw_params()`, `prepare()`, `trigger()`, `pointer()` per substream
- **auxv6**: Direct inline calls; no contract

### 3. Capture Unimplemented
**Status**: Read-side returns -1; no DMA, no ring buffer, no status tracking

**Required**:
- `audio_fileread()` function mapping to /dev/pcmC0D*c* nodes
- Capture ring buffer management (separate from playback)
- AC97 PCM-In DMA setup (parallel to playback)
- Poll readiness for capture readable events
- Capture-specific params, volume, state ioctls

### 4. Mixer Controls Unwired
**Status**: AC97 codec volume set during init; no ioctl exposure

**Required**: Map `AUDIO_IOC_SET_STREAM_VOL` and mixer device access to AC97 mixer registers (MASTER vol, PCM vol, input source select, etc.)

### 5. Tick-Based hw_ptr (Not Real Hardware Pointer)
**Current**: Consumption simulated via `audio_stream_consume_locked()` (lines 198–241) using ticks; advances hw_ptr at fixed rate regardless of actual hardware draining.

**Problem**: No true latency, position, or xrun detection feedback from hardware.

**Required**: Implement `pointer()` callback; AC97 can read CIV from hardware; compute hw_ptr frames from period count + buffer offset.

### 6. Profile-Based Capabilities (Not Hardware-Queried)
**Status**: Device capabilities hardcoded by device family (AC97→48kHz/1-2ch, etc.)

**Required**: real backends must expose actual format/rate list from hardware (AC97 can query codec; HDA exposes pin configs; etc.)

### 7. Non-Queued Control Mailbox
**File**: `user/audiod.c`, control mailbox at `/tmp/audiod.ctl`

**Problem**: Text commands dropped if two clients write simultaneously; no structured reply; no authentication.

**Required**: Control socket with command queueing, structured requests/replies, ownership checks.

---

## Comparative Analysis

### Linux ALSA Architecture
- **Mechanism**: Generic PCM core + driver ops (`snd_pcm_ops`); control ops (`snd_kcontrol_new`); buffer management via sound/core/pcm_memory.c
- **Hardware abstraction**: Callback-heavy; `snd_pcm_ops` defines open/close/hw_params/hw_free/prepare/trigger/pointer/ack/copy/silence/fill_silence,/status/delay
- **Mixing**: In-kernel software mixing via user-space ALSA lib plugins (dmix, upmix, etc.); kernel provides raw hardware stream
- **Complexity**: ~100K LOC core + drivers; rich but dense callback model; atomicity/locking via spinlocks + mutexes
- **Strength**: Unified driver interface; hardware variety well-supported; mature debugging infrastructure (proc, debugfs entries)
- **Weakness**: Complex callback state machines; steep learning curve for new backends; Mixing in userspace via plugin model adds latency

### NetBSD Audio Subsystem
- **Mechanism**: Device-independent upper layer (`audio.c`) + hardware-dependent layer (`audio_if.h` defines `audio_hw_if`)
- **Hardware abstraction**: Minimal callback set; `audio_hw_if` specifies `init_output/input()`, `set_params()`, `round_blocksize()`, `trigger_output/input()`, `halt_output/input()`, `pointer()`, `allocm()/freem()`, mixer ops
- **Mixing**: In-kernel virtual channels; device-independent layer multiplexes multiple `/dev/audio` opens onto one hardware stream; per-app policy in common layer
- **Complexity**: ~10K LOC core; simple callback model; mixing done once in common layer
- **Strength**: Smallest, most maintainable codebase; virtual channels handle multi-app transparently; clear separation of policy/mechanism
- **Weakness**: Limited to single hardware format at a time (others resampled); less hardware variety support

### auxv6 Audio Subsystem (Current Design)
- **Mechanism**: Native ABI (ioctls) + userspace daemon policy
- **Hardware abstraction**: None (direct AC97 calls; others unimplemented)
- **Mixing**: Daemon-side (skeleton present; not production-scaled)
- **Complexity**: ~2500 LOC kernel + daemon; simple per-stream state; mixing not yet bottlenecked
- **Strength**: Userspace mixing = flexible per-app policies (future: volume, routing, effects); small, auditable kernel footprint
- **Weakness**: First-running-stream hard limit; no formal hardware contract; mixing untested at scale; no capture yet

---

## Conclusion

**Verdict**: The design is **sound and differentiated** (userspace mixing is the right call for modularity), but execution is **incomplete and blocked by one architectural decision**. The first-running-stream selection was acceptable during scaffolding but must be addressed before multi-client support is viable.

**Path Forward**: 
1. Introduce formal `audio_hw_ops` contract (unblocks all backends, enables real capability queries)
2. Complete AC97 (real pointer callback, capture channel, mixer wiring)
3. Refactor daemon to true multi-client mixing (daemon opens single hardware stream, accepts multiple client connections, mixes + applies per-client policies)
4. Implement capture end-to-end (required before OSS compat)

**Timeline**: If executed sequentially, 8–12 weeks to parity with basic Linux/macOS behavior (multi-app playback, full AC97 duplex, mixer controls); another 8 weeks for OSS compat and HDA support.
