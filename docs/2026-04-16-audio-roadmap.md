# Audio Subsystem Development Roadmap
**Date**: April 16, 2026  
**Objective**: Bring auxv6 audio subsystem to Linux/macOS parity (multi-app playback, duplex I/O, mixer controls, OSS compatibility)  
**Status**: Ready for phased implementation

---

## Overview

This roadmap addresses the gap between the current scaffold (single-stream playback only) and production-ready audio behavior. The work is divided into **5 major milestones** spanning **12–16 weeks** at typical iteration pace.

**Key Architectural Assumption**: Daemon-side mixing is the chosen path (vs. kernel-side hardware stream binding). Daemon accepts multiple client connections, mixes to single hardware stream, applies per-client policies (volume, routing future). This preserves the design intent (userspace policy + kernel mechanism) and simplifies the kernel layer.

---

## Milestone 1: Formalize Hardware Abstraction Layer (Weeks 1–2)

### Goal
Introduce formal `struct audio_hw_ops` contract (NetBSD-inspired) to decouple core from backend implementations. This enables robust multi-backend support and real capability queries.

### Deliverables

#### 1.1 Create `include/audio_hwif.h`
Define `struct audio_hw_ops` with required callbacks:
```c
struct audio_hw_ops {
  // Format/parameter negotiation
  int (*query_format)(void *hdl, struct audio_format *fmt);
  int (*set_format)(void *hdl, struct audio_format *fmt);
  int (*round_blocksize)(void *hdl, int bs, int mode);
  
  // Stream lifecycle
  int (*init_output)(void *hdl);
  int (*init_input)(void *hdl);
  int (*trigger_output)(void *hdl);
  int (*trigger_input)(void *hdl);
  int (*halt_output)(void *hdl);
  int (*halt_input)(void *hdl);
  
  // DMA position and properties
  uint32_t (*pointer)(void *hdl, int mode);  // Return hw_ptr frame offset
  int (*get_props)(void *hdl, struct audio_props *props);
  
  // Memory allocation
  void *(*allocm)(void *hdl, int direction, size_t size, int type);
  void (*freem)(void *hdl, void *addr, int type);
  
  // Mixer controls (optional)
  int (*mixer_set_port)(void *hdl, mixer_ctrl_t *mc);
  int (*mixer_get_port)(void *hdl, mixer_ctrl_t *mc);
};

struct audio_hw_device {
  struct audio_hw_ops *ops;
  void *hdl;  // Private hardware handle
};
```

**Files**:
- Create `include/audio_hwif.h` (~100 lines)
- Update `include/audio.h` to include `audio_hwif.h`

**Validation**: Compiles; no runtime errors yet (purely interface definition)

#### 1.2 Refactor `kernel/audio/audio_core.c` to Use Hardware Ops
Update core to dispatch via ops table instead of direct AC97 calls:
- Replace direct `audio_intel_ac97_*()` calls with `hw_device->ops->*()` dispatch
- Maintain backward compatibility: AC97 registers itself via `audio_attach_hwif()`
- Update `audio_hw_period_advance()` to call `pointer()` callback (real hw_ptr, not ticks)
- Lines affected: ~200 lines in audio_core.c (dispatch, pointer query, trigger calls)

**Validation**: AC97 still probes, daemon starts, playback works (functional equivalence test)

---

## Milestone 2: Complete AC97 Backend (Weeks 2–4)

### Goal
Convert AC97 from partial (playback only, synthesized hw_ptr) to complete (duplex, real pointer, mixer wiring).

### Deliverables

#### 2.1 Implement Real `pointer()` Callback
Read CIV (Current Index Value) from AC97 hardware to compute true hw_ptr:
```c
static uint32_t ac97_pointer(void *hdl, int mode) {
  struct ac97_softc *sc = hdl;
  uint32_t civ, offset;
  
  if (mode == AUMODE_PLAY) {
    civ = AC97_READ_CIV_OUT(sc->nabm_base);
  } else if (mode == AUMODE_RECORD) {
    civ = AC97_READ_CIV_IN(sc->nabm_base);
  } else {
    return 0;
  }
  
  // CIV is buffer descriptor index (0–31); each descriptor is 2KB
  offset = (civ * 2048) / sizeof(int16_t);  // Convert bytes to frames
  return offset;
}
```

**Impact**: 
- `audio_core.c` calls `pointer()` instead of synthesizing from ticks
- True latency measurement possible
- Xrun detection becomes hardware-based (frame count vs. hw_ptr, not simulated)

**Files**:
- Modify `kernel/driver/audio_intel_ac97.c` (~50 lines added)
- Modify `kernel/audio/audio_core.c` to call pointer callback (~30 lines changed)

**Validation**: `audiostat` now shows real hw_ptr; latency matches hardware rate

#### 2.2 Implement Capture DMA Path
Setup and manage PCM-In channel (parallel to playback):
- Add capture BDL array (separate from playback)
- Implement `ac97_init_input()`: Configure PCM-In rate/format
- Implement `ac97_trigger_input()`: Enable PCMIN.Start
- Implement `ac97_halt_input()`: Disable PCM-In
- Update interrupt handler to service both PCM-Out and PCM-In
- Update `audio_hw_period_advance()` to handle input mode (copy from DMA bounce to ring)

**Files**:
- Modify `kernel/driver/audio_intel_ac97.c` (~150 lines added)
- Modify `kernel/audio/audio_core.c` for input handoff (~40 lines added)

**Validation**: Capture stream can be opened, started, and delivers data to `audioctl record`

#### 2.3 Wire Mixer Controls
Expose AC97 mixer registers via native ioctl:
- Implement `ac97_mixer_set_port()` and `ac97_mixer_get_port()` callbacks
- Map common controls: MASTER volume, PCM volume, input source select
- Update `audio_core.c` ioctl dispatch to handle mixer control ioctls (new: AUDIO_IOC_SET_MIXER_CONTROL, AUDIO_IOC_GET_MIXER_CONTROL)
- Create `user/audiomixerctl.c` CLI tool to query/set mixer controls

**Files**:
- Modify `kernel/driver/audio_intel_ac97.c` (~80 lines added)
- Modify `kernel/audio/audio_core.c` (~50 lines added; new mixer ioctls)
- Create `user/audiomixerctl.c` (~150 lines; CLI tool)

**Validation**: `audiomixerctl get master`, `audiomixerctl set master 75` work; volume changes audible

#### 2.4 Implement AC97 `audio_hw_ops` Registration
Formalize AC97 as registered hardware backend:
```c
static struct audio_hw_ops ac97_hwif = {
  .query_format = ac97_query_format,
  .set_format = ac97_set_format,
  .round_blocksize = ac97_round_blocksize,
  .init_output = ac97_init_output,
  .init_input = ac97_init_input,
  .trigger_output = ac97_trigger_output,
  .trigger_input = ac97_trigger_input,
  .halt_output = ac97_halt_output,
  .halt_input = ac97_halt_input,
  .pointer = ac97_pointer,
  .get_props = ac97_get_props,
  .mixer_set_port = ac97_mixer_set_port,
  .mixer_get_port = ac97_mixer_get_port,
};
```

Call `audio_attach_hwif(&ac97_hwif, &softc)` during probe.

**Files**:
- Modify `kernel/driver/audio_intel_ac97.c` (~50 lines added; registration)
- Modify `kernel/audio/audio_core.c` registration path (~30 lines changed; hwif registration vs. direct call)

**Validation**: AC97 probes and registers via hardware ops; all callbacks functional

---

## Milestone 3: Refactor Daemon for Multi-Client Mixing (Weeks 4–6)

### Goal
Transform daemon from single-stream policy to true multi-client mixing hub. The daemon becomes the policy layer: it opens multiple client connections, mixes their streams to single hardware stream, applies per-client volume/effects.

### Deliverables

#### 3.1 Implement Client Connection Management
Replace mailbox with Unix domain socket server:
- Create `/tmp/audiod.sock` as listen socket
- Daemon accepts multiple concurrent client connections (use poll array)
- Each client gets its own 16KB ring buffer (separate from hardware ring)
- Client sends audio data and control commands over same socket

```c
struct audio_client {
  int fd;
  char *ring_buf;  // 16KB client-side ring
  uint32_t head, tail;  // Ring pointers
  int16_t volume;  // Per-client volume (0–32767)
  // ... other client state
};
```

**Files**:
- Rewrite `user/audiod.c` (~600 lines; socket server + client array)

**Validation**: Multiple `audioctl` processes can open socket and write simultaneously without blocking each other

#### 3.2 Implement Mixing Loop
Daemon poll loop now calls mixing function:
```c
/* In daemon poll loop: */
while(1) {
  poll(pollfd, num_fds, timeout);
  
  /* Receive data from all clients into their rings */
  for each client {
    if (pollfd[client].revents & POLLIN) {
      read_from_client_into_ring();
    }
  }
  
  /* Mix all active client rings into hardware ring */
  mix_clients_to_hw_ring();
  
  /* Write from hardware ring to /dev/pcmC0D0p */
  if (hw_ring_has_data) {
    write_to_hw();
  }
}
```

Mixing function:
```c
static void mix_clients_to_hw_ring() {
  int32_t accum[2048];  // Per-frame accumulator (S32)
  
  /* Clear accumulator */
  memset(accum, 0, sizeof(accum));
  
  /* Accumulate each client's contribution */
  for each client {
    for each frame {
      int16_t *src = client_ring[client_tail:];
      accum[frame] += (src * client->volume) >> 15;  // Apply per-client gain
    }
  }
  
  /* Clip and quantize to S16_LE */
  int16_t *dst = hw_ring[hw_tail:];
  for each frame {
    if (accum[frame] > 32767) accum[frame] = 32767;
    if (accum[frame] < -32768) accum[frame] = -32768;
    dst[frame] = accum[frame];
  }
  
  hw_ring.tail += frames;
}
```

**Files**:
- Rewrite `user/audiod.c` mixing logic (~150 lines; replaces old silence write with real mixing)

**Validation**: Three `audioctl` clients sending audio; daemon mixes to one audible output, no clipping distortion

#### 3.3 Implement Per-Client Control Interface
Structured control commands over client socket (replace mailbox text format):
```c
struct audio_client_cmd {
  uint32_t cmd;  // Commands: SET_VOLUME, GET_STATUS, etc.
  uint32_t param;
  char data[256];
};
```

Supported commands:
- `GET_STATUS`: Return daemon status (mixing rate, active clients, hw_ptr)
- `SET_VOLUME <0–32767>`: Set per-client volume
- `QUERY_CAPS`: Return hardware capabilities

**Files**:
- Implement `audio_client_cmd` struct in `include/audio.h` (~20 lines)
- Implement command parsing in daemon (~80 lines)
- Update `user/audioctl.c` to use socket instead of `/dev/audio` node (~100 lines changed)

**Validation**: `audioctl set-volume 50` changes that client's volume; multiple clients have independent volume control

#### 3.4 Implement Control Daemon Authentication
Protect daemon from arbitrary reconfiguration:
- Daemon tracks client ownership (UID/GID from socket peer credentials)
- Only client that opened stream can control its volume or parameters
- Root/daemon owner can query global status

**Files**:
- Modify `user/audiod.c` (`getpeereid()` call, ownership check) (~30 lines)

**Validation**: Unprivileged user cannot change another user's client volume; root can query any client

---

## Milestone 4: Implement Capture End-to-End (Weeks 6–8)

### Goal
Wire read-side I/O complete. Users can record audio with `audioctl record`. Capture ring, ioctls, poll readiness, and procfs all working.

### Deliverables

#### 4.1 Implement `audio_fileread()` in Core
Add read(2) support to `/dev/pcmC0D*c` (capture) nodes:
```c
int audio_fileread(struct file *f, char *addr, int n) {
  struct audio_stream *s = audio_stream_find_locked(f);
  uint32_t to_read = min(n, audio_stream_used_bytes(s));
  
  audio_stream_copy_from_ring(s, addr, to_read);  // Wrap-aware copy
  s->ring.head += to_read;
  return to_read;
}
```

**Files**:
- Add `audio_fileread()` to `kernel/audio/audio_core.c` (~60 lines)
- Modify VFS dispatch to call `audio_fileread()` for read(2) on audio fd

**Validation**: `cat /dev/pcmC0D0c | hexdump` returns capture data

#### 4.2 Implement Capture Ioctls
Add capture-specific parameter control:
- `AUDIO_IOC_SET_CAPTURE_PARAMS`: Set capture rate, channels, format
- `AUDIO_IOC_GET_CAPTURE_PARAMS`: Query current capture params
- `AUDIO_IOC_CAPTURE_START`/`CAPTURE_STOP`/`CAPTURE_PREPARE`: State transitions
- Update `AUDIO_IOC_GET_STATUS` to return capture-specific state

**Files**:
- Modify `kernel/audio/audio_core.c` ioctl dispatch (~80 lines added)
- Update `user/audioctl.c` with `record` subcommand (~60 lines added)

**Validation**: `audioctl record 48000 2 S16_LE` works; record subprocess receives samples

#### 4.3 Implement Capture Poll Readiness
Update `audio_poll_events()` to return readable when capture data available:
```c
if (mode == AUMODE_RECORD) {
  if (audio_stream_used_bytes(s) > 0) return POLLIN;
  if (s->stream_state == AUDIO_ST_XRUN) return POLLERR;
}
```

**Files**:
- Modify `kernel/audio/audio_core.c` (~10 lines; poll dispatch)

**Validation**: `poll()` on record fd returns POLLIN when data available

#### 4.4 Expose Capture in Procfs
Update `/proc/audio_clients` to show capture streams and their queue state:
- Mark stream direction (PLAY/REC)
- Show capture-specific counters (samples captured, xruns, late deliveries)

**Files**:
- Modify `kernel/audio/audio_core.c` procfs output (~20 lines)

**Validation**: `audiostat` shows active capture streams with accurate pointers and stats

---

## Milestone 5: Add HDA Backend Stub (Weeks 8–10)

### Goal
Implement Intel HDA backend (most common modern audio hardware). Demonstrates multi-backend architecture; enables HDA-based testing on newer machines.

### Deliverables

#### 5.1 Implement HDA Probe and PCI Enumeration
Full device probe (matching existing stub locations):
- PCI BAR mapping (MMIO only; HDA uses mapped registers)
- Configure and reset codec hub
- Enumerate codec functions (audio outputs, inputs, DSPs)
- Register via `audio_attach_hwif()`

**Files**:
- Rewrite `kernel/driver/audio_intel_hda.c` (~300 lines; from ~17 line stub)

**Validation**: `dmesg` shows HDA probe and codec detection

#### 5.2 Implement HDA Playback Stream
Setup playback stream infrastructure:
- DMA ring setup (HDA uses scatter-gather descriptor lists)
- Stream position buffer read
- Interrupt service for period completion
- Format negotiation (HDA supports up to 192kHz)

**Files**:
- Add playback stream logic to `kernel/driver/audio_intel_hda.c` (~150 lines)

**Validation**: Playback works on HDA hardware; `audiostat` shows active playback

#### 5.3 Implement HDA Mixer Controls
Expose HDA codec mixer (typically Realtek, Conexant, or generic):
- Pin configuration (output select, input source)
- Volume/mute controls
- Map to `audio_hw_ops` mixer callbacks

**Files**:
- Add mixer support to `kernel/driver/audio_intel_hda.c` (~100 lines)

**Validation**: `audiomixerctl` works on HDA; headphone jack detect functional

#### 5.4 Implement HDA Capture
Wire PCM-In streams (parallel to playback):
- Capture ring setup
- Format negotiation
- Interrupt service
- Per-device capture supported

**Files**:
- Add capture stream logic to `kernel/driver/audio_intel_hda.c` (~100 lines)

**Validation**: `audioctl record` works on HDA hardware

---

## Milestone 6: OSS Compatibility Layer (Weeks 10–14)

### Goal
Enable legacy `/dev/dsp` and `/dev/mixer` access. Popular tools (sox, ffmpeg, GAIM voice) can use auxv6 audio without modification.

### Deliverables

#### 6.1 Implement OSS `/dev/dsp` Emulation
Virtual device that translates OSS ioctls to native API:
```c
struct oss_dsp_file {
  int native_fd;  // Underlying `/dev/pcmC0D0p` or `/dev/pcm*c`
  int mode;  // DOPEN_READ, DOPEN_WRITE, DOPEN_RDWR
  struct audio_params params;
};
```

**Files**:
- Create `kernel/driver/audio_oss.c` (~400 lines; oss_dsp_ioctl translate, read/write dispatch)
- Register `/dev/dsp` device

**Validation**: `aplay` and `arecord` from alsa-utils work on `/dev/dsp`

#### 6.2 Implement OSS `/dev/mixer` Emulation
Virtual device for mixer control:
- Translate `MIXER_WRITE(SOUND_MIXER_VOLUME)` to `AUDIO_IOC_SET_STREAM_VOL`
- Expose hardware mixer controls (volume, input source, etc.)

**Files**:
- Add mixer support to `kernel/driver/audio_oss.c` (~150 lines)
- Register `/dev/mixer` device

**Validation**: `aumix` or `alsamixer` works on `/dev/mixer`

#### 6.3 Implement ALSA Plugin Shim (Userspace)
Optional userspace plugin for ALSA-aware applications (e.g., PulseAudio compatibility):
- Simple alsa-conf plugin that dispatches `snd_pcm_*()` calls to `/dev/dsp`
- Enables ALSA apps to use auxv6 audio backend

**Files**:
- Create `user/alsa-plugin-auxv6.c` (~200 lines; ALSA PCM plugin interface)
- Install as `.so` in `/usr/lib/alsa-lib/`

**Validation**: `speaker-test` from alsa-utils works via plugin; pulseaudio can enumerate auxv6 as sink/source

---

## Milestone 7: Testing and Validation (Weeks 14–16)

### Goal
Comprehensive testing of multi-client mixing, duplex I/O, hardware backends, and compatibility interfaces.

### Deliverables

#### 7.1 Multi-Client Integration Test
Test suite for concurrent clients:
```c
/* Test: 3 clients writing sine waves of different frequencies */
test_parallel_playback_3way() {
  client1 = audioctl_open();
  client2 = audioctl_open();
  client3 = audioctl_open();
  
  /* Each writes 1 second of audio */
  audioctl_write(client1, sine_440hz, size);
  audioctl_write(client2, sine_880hz, size);
  audioctl_write(client3, sine_220hz, size);
  
  /* Verify output is sum of three waves (FFT analysis) */
}
```

**Files**:
- Create `user/tests/audio_multitest.c` (~200 lines; multi-client scenarios)

**Validation**: Run regression suite; no dropouts, no clipping on sum of 3 clients at comfortable levels

#### 7.2 Duplex Test
Simultaneous capture + playback:
```c
test_duplex() {
  capture_fd = open("/dev/pcmC0D0c");
  playback_fd = open("/dev/pcmC0D0p");
  
  /* Record 5 seconds while playing a tone */
  /* Verify recorded audio contains input signal + playback tone */
}
```

**Files**:
- Create `user/tests/audio_duplextest.c` (~100 lines)

**Validation**: No deadlock, no xrun on duplex operation, quality acceptable

#### 7.3 Hardware Backend Compatibility Test
Verify AC97 and HDA produce equivalent results:
```c
test_backend_consistency() {
  /* If AC97 hardware available: play test file, record output */
  /* If HDA hardware available: repeat same test on HDA */
  /* Compare spectral content (FFT, ensure peaks match) */
}
```

**Files**:
- Create `user/tests/audio_backend_compare.c` (~150 lines)

**Validation**: AC97 and HDA backends produce similar frequency response

#### 7.4 OSS Compatibility Test
Verify legacy tools work:
```bash
# Test 1: aplay on /dev/dsp
aplay /usr/share/sounds/freedesktop/stereo/complete.oga > /dev/dsp

# Test 2: arecord from /dev/dsp
arecord -f cd /tmp/recorded.wav

# Test 3: sox through /dev/dsp
sox -n /dev/dsp synth 5 sine 440

# Test 4: aumix on /dev/mixer
aumix -v 75
```

**Validation**: All four tests pass; no seg faults or hang conditions

---

## Implementation Sequencing

**Recommended Order**:
1. **Weeks 1–2**: Milestone 1 (Hwif abstraction) — foundation for all backends
2. **Weeks 2–4**: Milestone 2 (AC97 completion) — first robust backend
3. **Weeks 4–6**: Milestone 3 (Daemon multi-client) — core policy layer
4. **Weeks 6–8**: Milestone 4 (Capture) — duplex I/O ready
5. **Weeks 8–10**: Milestone 5 (HDA backend) — modern hardware support
6. **Weeks 10–14**: Milestone 6 (OSS compat) — legacy app support
7. **Weeks 14–16**: Milestone 7 (Testing) — quality assurance + regression suite

**Parallelizable Tasks**:
- Milestone 2 and 3 can overlap (2.x up to 2.3, then 3.x in parallel, then 2.4 + 3.4)
- Milestone 5 can start during Milestone 4 (HDA probe independent of capture wiring)

**Estimated Effort**:
- Total: ~3000 lines of code (kernel + userspace combined)
- Kernel audio core: +~300 lines (hwif dispatch, capture, real pointer)
- AC97 backend: +~300 lines (capture, mixer, pointer callback)
- Daemon: +~500 lines (socket server, multi-client mixing, structured control)
- HDA backend: +~600 lines (probe, playback, capture, mixer)
- OSS compat: +~500 lines (dsp, mixer emulation)
- Testing: +~400 lines (integration, duplex, compatibility)
- Total: ~3000 lines

---

## Success Criteria

**Milestone 1 Complete**: 
- [ ] `audio_hwif.h` defined and compiles
- [ ] AC97 registers via `audio_attach_hwif()` without compilation errors
- [ ] Playback functionality unchanged (regression test passes)

**Milestone 2 Complete**:
- [ ] `audiostat` shows real hw_ptr (not synthesized from ticks)
- [ ] `audioctl record` delivers audio data (capture stream works)
- [ ] `audiomixerctl set master 50` changes volume audibly
- [ ] Duplex (simultaneous play + record) does not xrun

**Milestone 3 Complete**:
- [ ] Three concurrent `audioctl` instances write simultaneously to daemon
- [ ] Output is sum of all three streams (spectrally)
- [ ] No dropouts or clipping
- [ ] Per-client volume control works (`audiodctl set-volume -i <client> 75`)

**Milestone 4 Complete**:
- [ ] `arecord -f cd /tmp/test.wav` captures audio without hang
- [ ] `audioctl record` and `audioctl play` work on same hardware simultaneously

**Milestone 5 Complete**:
- [ ] HDA hardware probes and registers `audio_hwif`
- [ ] `audioctl play` works on HDA
- [ ] `audioctl record` works on HDA
- [ ] Mixer controls expose HDA codec functions

**Milestone 6 Complete**:
- [ ] `aplay /usr/share/sounds/freedesktop/stereo/complete.oga > /dev/dsp` produces audio
- [ ] `arecord -f cd /tmp/test.wav < /dev/dsp` records
- [ ] `aumix -v 75` changes hardware volume visibly
- [ ] No OSS-specific crashes or hangs

**Milestone 7 Complete**:
- [ ] `audio_multitest.c` passes all scenarios
- [ ] `audio_duplextest.c` passes without xrun
- [ ] `audio_backend_compare.c` shows < 5% frequency deviation between AC97 and HDA
- [ ] All OSS compatibility tests pass

---

## Risk Mitigation

| Risk | Likelihood | Mitigation |
|------|-----------|-----------|
| `pointer()` callback read race (CIV changes during read) | Low | Implement pointer as critical section (disable IRQ briefly); validate CIV consistency |
| HDA codec detection fails on some boards | Medium | Probe multiple codec addresses; support fallback to profile defaults; extensive hardware testing |
| Daemon mixing introduces glitches (software latency) | Medium | Real-time priority on daemon process; reduce mixing latency loop via larger hardware period |
| OSS/ALSA compatibility incomplete (edge-case ioctls) | Medium | Thorough comparison with real `/dev/dsp` behavior on Linux; add unsupported ioctl warnings |
| Scaling issues with >4 concurrent clients | Low | Monitor CPU usage; implement client count limit and graceful rejection; add bandwidth check |

---

## Appendices

### A. File Manifest: New and Modified Files

**New Files** (all ~100–600 lines each):
- `include/audio_hwif.h` — Hardware ops contract (100 lines)
- `user/audiomixerctl.c` — Mixer control CLI (150 lines)
- `kernel/driver/audio_intel_hda.c` — HDA backend (600 lines; replaces 17-line stub)
- `kernel/driver/audio_oss.c` — OSS compatibility (550 lines)
- `user/alsa-plugin-auxv6.c` — ALSA plugin shim (200 lines)
- `user/tests/audio_multitest.c` — Multi-client integration test (200 lines)
- `user/tests/audio_duplextest.c` — Duplex test (100 lines)
- `user/tests/audio_backend_compare.c` — Backend consistency test (150 lines)

**Modified Files** (40–300 lines changed each):
- `kernel/audio/audio_core.c` — Hwif dispatch, capture path, real pointer (~400 lines)
- `kernel/driver/audio_intel_ac97.c` — Capture, mixer, pointer callback (~300 lines)
- `include/audio.h` — Hwif include, new ioctl constants (~50 lines)
- `user/audiod.c` — Socket server, multi-client mixing (~600 lines rewritten)
- `user/audioctl.c` — Socket client, new subcommands (~100 lines)

### B. Testing Checklist (Pre-Deployment)

**AC97 Hardware (if available)**:
- [ ] Probe detects codec (dmesg log)
- [ ] `audioctl play` produces sound (1kHz tone test)
- [ ] `audioctl record | hexdump` shows samples
- [ ] Duplex: simultaneous play + record (no xrun)
- [ ] Volume control changes level
- [ ] Long-running playback (>10 min) doesn't degrade

**HDA Hardware (if available)**:
- [ ] Probe detects codec and pin configs
- [ ] Same tests as AC97 (play, record, duplex, volume, stability)
- [ ] Frequency response similar to AC97 (spectrally)

**Daemon Multi-Client**:
- [ ] 3× `audioctl play` + mixer test simultaneously
- [ ] Volume control on each client independently
- [ ] No clipping on combined output
- [ ] Daemon survives client crash (one client closes abruptly)

**OSS Compat**:
- [ ] `sox -n /dev/dsp synth 5 sine 440` plays 5-second tone
- [ ] `arecord -f cd /tmp/test.wav; aplay /tmp/test.wav` works end-to-end
- [ ] Legacy applications don't seg fault

---

## Timeline Summary

| Phase | Duration | Key Deliverable | Status |
|-------|----------|-----------------|--------|
| 1. Hwif Abstraction | 2 weeks | Hardware ops contract | Not started |
| 2. AC97 Completion | 2 weeks | Duplex, mixer, real pointer | Not started |
| 3. Daemon Multi-Client | 2 weeks | Socket server, mixing hub | Not started |
| 4. Capture Path | 2 weeks | Read-side I/O fully wired | Not started |
| 5. HDA Backend | 2 weeks | Intel HDA probed and functional | Not started |
| 6. OSS Compat | 4 weeks | `/dev/dsp`, `/dev/mixer`, ALSA plugin | Not started |
| 7. Testing & QA | 2 weeks | Full regression + compatibility suite | Not started |
| **Total** | **16 weeks** | **Linux/macOS parity** | |

---

## Conclusion

This roadmap transforms auxv6 audio from "working scaffold" to "production-ready subsystem" supporting:
- **Multi-app concurrency** (daemon mixes multiple clients)
- **Duplex I/O** (simultaneous record + playback)
- **Hardware variety** (AC97, HDA, extensible to others)
- **Legacy compatibility** (OSS `/dev/dsp`, ALSA plugins)
- **Quality assurance** (comprehensive regression suite)

The design is sound; execution is systematic and parallelizable. Estimated 12–16 week timeline at normal iteration pace.
