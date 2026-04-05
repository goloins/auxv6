# Audio Stage 0 Contract Pack

## Status

This document defines the Stage 0 contract for the audio subsystem and is intended to be the implementation source-of-truth for:

- native audio ABI (`/dev/audioctl`, `/dev/pcm*`, `AUDIO_IOC_*`), and
- OSS compatibility mapping (`/dev/dsp`, `/dev/mixer`, OSS ioctl translation).

It is aligned with:

- `docs/audio-subsystem-design.md`
- `docs/audio-subsystem-implementation-plan.md`

---

## 1) Contract Goals

1. Freeze a stable v1 native ABI before driver complexity grows.
2. Make struct layout and ioctl behavior explicit enough to avoid piecemeal drift.
3. Define OSS compatibility expectations now, even if implemented after native stability stages.
4. Keep room for forward-compatible extension without breaking existing binaries.

---

## 2) Device Model and Node Naming

## Native nodes

- `/dev/audioctl`:
  - global control and enumeration endpoint
  - no direct PCM data payload writes
- `/dev/pcmC<card>D<device>p`:
  - playback endpoint
- `/dev/pcmC<card>D<device>c`:
  - capture endpoint (reserved in Stage 0; may return unsupported until capture tranche)

Examples:

- `/dev/pcmC0D0p`
- `/dev/pcmC1D0p`

## Compatibility nodes (reserved in Stage 0)

- `/dev/dsp`
- `/dev/mixer`

These names are reserved for Stage 5 OSS-first compatibility.

---

## 3) ABI Versioning Policy

## Global constants

```c
#define AUDIO_ABI_MAJOR 1
#define AUDIO_ABI_MINOR 0
#define AUDIO_ABI_PATCH 0
#define AUDIO_ABI_VERSION ((AUDIO_ABI_MAJOR << 16) | (AUDIO_ABI_MINOR << 8) | AUDIO_ABI_PATCH)
```

## Rules

1. All top-level ioctl payload structs include:
- `uint32_t abi_version`
- `uint32_t struct_size`
- reserved padding fields

2. Kernel behavior:
- reject `struct_size` smaller than minimum required for command
- accept larger `struct_size` if trailing bytes are ignored for forward compatibility
- reject incompatible `AUDIO_ABI_MAJOR`

3. Userspace behavior:
- always set `struct_size = sizeof(struct payload)`
- zero all reserved bytes

---

## 4) Native Core Types (Header Contract)

Recommended header:

- `include/audio.h`

## Enumerations

```c
enum audio_direction {
  AUDIO_DIR_PLAYBACK = 0,
  AUDIO_DIR_CAPTURE  = 1,
};

enum audio_sample_format {
  AUDIO_FMT_S16_LE = 0,
  AUDIO_FMT_S24_LE = 1,
  AUDIO_FMT_S32_LE = 2,
  AUDIO_FMT_U8     = 3,
};

enum audio_stream_state {
  AUDIO_ST_NEW        = 0,
  AUDIO_ST_CONFIGURED = 1,
  AUDIO_ST_PREPARED   = 2,
  AUDIO_ST_RUNNING    = 3,
  AUDIO_ST_XRUN       = 4,
  AUDIO_ST_STOPPED    = 5,
  AUDIO_ST_DRAINED    = 6,
  AUDIO_ST_CLOSED     = 7,
};
```

## Common IDs

```c
struct audio_device_id {
  uint16_t card;
  uint16_t device;
  uint16_t direction; /* enum audio_direction */
  uint16_t reserved0;
};
```

## Capability/format descriptors

Device flags (native + compatibility intent):

```c
#define AUDIO_DEVF_CAN_PLAYBACK      0x00000001U
#define AUDIO_DEVF_CAN_CAPTURE       0x00000002U
#define AUDIO_DEVF_OSS_DSP_COMPAT    0x00010000U
#define AUDIO_DEVF_OSS_MIXER_COMPAT  0x00020000U
```

```c
#define AUDIO_MAX_FORMATS   8
#define AUDIO_MAX_RATES    16

struct audio_hw_caps {
  uint32_t abi_version;
  uint32_t struct_size;

  uint16_t card;
  uint16_t device;
  uint16_t direction;
  uint16_t reserved0;

  uint32_t min_channels;
  uint32_t max_channels;
  uint32_t min_rate;
  uint32_t max_rate;

  uint32_t format_count;
  uint32_t formats[AUDIO_MAX_FORMATS];

  uint32_t rate_count;
  uint32_t rates[AUDIO_MAX_RATES];

  uint32_t min_period_frames;
  uint32_t max_period_frames;
  uint32_t min_periods;
  uint32_t max_periods;

  uint32_t flags;
  uint32_t reserved1[7];
};
```

## Stream params

```c
struct audio_stream_params {
  uint32_t abi_version;
  uint32_t struct_size;

  uint32_t sample_rate;
  uint32_t channels;
  uint32_t sample_format;     /* enum audio_sample_format */

  uint32_t period_frames;
  uint32_t periods;
  uint32_t buffer_frames;     /* optional explicit total; 0 = derived */

  uint32_t flags;
  uint32_t reserved0[8];
};
```

## Stream status

```c
struct audio_stream_status {
  uint32_t abi_version;
  uint32_t struct_size;

  uint32_t state;             /* enum audio_stream_state */
  uint32_t flags;

  uint64_t hw_ptr_frames;
  uint64_t sw_ptr_frames;
  uint64_t queued_frames;
  uint64_t delay_frames;

  uint32_t xruns;
  uint32_t late_wakeups;
  uint32_t period_misses;
  uint32_t recoveries;

  uint64_t monotonic_ns;
  uint32_t reserved0[6];
};
```

## Stream volume

```c
#define AUDIO_VOL_MIN_DB_Q8_8  (-9600)  /* -37.5 dB example floor; policy-defined */
#define AUDIO_VOL_MAX_DB_Q8_8  (0)

struct audio_stream_volume {
  uint32_t abi_version;
  uint32_t struct_size;

  int32_t left_db_q8_8;
  int32_t right_db_q8_8;

  uint32_t mute;
  uint32_t reserved0[7];
};
```

Note:

- `db_q8_8` is fixed-point dB to avoid float ABI dependencies in kernel interfaces.

---

## 5) Native IOCTL Numbering Contract

Recommended header:

- `include/audio_ioctl.h`

Use fixed numeric ioctl request values (Linux-style hex space already used in tree) rather than introducing `_IO*` macros in Stage 0.

## Magic and range

```c
#define AUDIO_IOC_BASE  0x5600
```

## Control endpoint ioctls (`/dev/audioctl`)

```c
#define AUDIO_IOC_QUERY_ABI      0x5600 /* in/out: struct audio_abi_info */
#define AUDIO_IOC_ENUM_DEVICES   0x5601 /* in/out: struct audio_enum_devices */
#define AUDIO_IOC_QUERY_CAPS     0x5602 /* in/out: struct audio_hw_caps */
#define AUDIO_IOC_SET_DEFAULT    0x5603 /* in:     struct audio_default_route */
#define AUDIO_IOC_GET_DEFAULT    0x5604 /* in/out: struct audio_default_route */
```

## PCM endpoint ioctls (`/dev/pcm*`)

```c
#define AUDIO_IOC_SET_PARAMS     0x5610 /* in:     struct audio_stream_params */
#define AUDIO_IOC_GET_PARAMS     0x5611 /* in/out: struct audio_stream_params */
#define AUDIO_IOC_PREPARE        0x5612 /* in/out: struct audio_cmd */
#define AUDIO_IOC_START          0x5613 /* in/out: struct audio_cmd */
#define AUDIO_IOC_STOP           0x5614 /* in/out: struct audio_cmd */
#define AUDIO_IOC_DRAIN          0x5615 /* in/out: struct audio_cmd */
#define AUDIO_IOC_DROP           0x5616 /* in/out: struct audio_cmd */
#define AUDIO_IOC_GET_STATUS     0x5617 /* in/out: struct audio_stream_status */
#define AUDIO_IOC_SET_STREAM_VOL 0x5618 /* in:     struct audio_stream_volume */
#define AUDIO_IOC_GET_STREAM_VOL 0x5619 /* in/out: struct audio_stream_volume */
#define AUDIO_IOC_RESET_XRUN     0x561A /* in/out: struct audio_cmd */
```

Minimal shared command payload:

```c
struct audio_cmd {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t flags;
  uint32_t reserved0;
};
```

## Compatibility reservation range

```c
#define AUDIO_IOC_COMPAT_BASE    0x5680
#define AUDIO_IOC_COMPAT_END     0x56FF
```

Reserved for future compatibility control plumbing without colliding with native controls.

---

## 6) File Operation and Poll Semantics

## `/dev/audioctl`

- `read`:
  - optional text/structured status snapshot (not required Stage 0)
- `write`:
  - not required
- `ioctl`:
  - required for enumeration/caps/default-route control
- `poll`:
  - readable when async topology/status event is pending (optional Stage 0, required later)

## `/dev/pcm*`

- `write` (playback):
  - blocking mode: waits for writable space
  - nonblocking mode: returns `-EAGAIN` if no writable space
- `read` (capture):
  - reserved; may return unsupported until capture phase
- `poll`:
  - playback fd writable when write-space >= wake threshold
  - error/hangup bits on backend fatal stream failure

## Suggested poll bits contract

- `POLLOUT`: playback writable space available
- `POLLIN`: capture readable frames available (future)
- `POLLERR`: backend failure or unrecoverable stream state
- `POLLHUP`: stream endpoint invalidated (device detach/reset unrecoverable)

---

## 7) Stream State Transition Contract

Valid transitions:

- `NEW -> CONFIGURED`
- `CONFIGURED -> PREPARED`
- `PREPARED -> RUNNING`
- `RUNNING -> STOPPED`
- `RUNNING -> XRUN`
- `XRUN -> PREPARED` (after `AUDIO_IOC_RESET_XRUN` or `AUDIO_IOC_PREPARE` policy)
- `STOPPED -> PREPARED`
- `STOPPED -> DRAINED` (if empty)
- any non-`CLOSED` state -> `CLOSED` on `close`

Invalid transitions return deterministic error (`-EINVAL` or state-specific `-EPIPE` for xrun cases).

---

## 8) Error Contract

Recommended error mapping:

- `-EINVAL`:
  - invalid enum/field/range
  - invalid state transition
- `-E2BIG`:
  - oversized payload exceeding accepted struct policy
- `-ENOTSUP` or `-EOPNOTSUPP`:
  - unsupported format/rate/channels/operation on backend
- `-EAGAIN`:
  - nonblocking write/read would block
- `-EPIPE`:
  - xrun condition on data-path operations
- `-EBUSY`:
  - exclusive open conflict (if exclusive mode enabled)
- `-ENODEV`:
  - device disappeared or unavailable
- `-EIO`:
  - hardware/backend I/O failure

Kernel must avoid ambiguous generic `-1` returns in audio path once Stage 1 lands.

---

## 9) Native Defaults (Policy)

Stage 0/1 defaults for deterministic bring-up:

- rate: 48000 Hz
- channels: 2
- format: S16_LE
- period: 256 frames
- periods: 4
- total buffer: 1024 frames

These defaults should be surfaced by both `AUDIO_IOC_GET_PARAMS` and `/proc/audio` summaries.

---

## 10) OSS Compatibility Contract (Stage 5 Target)

## Scope of first-class OSS support

- `/dev/dsp` playback path
- `/dev/mixer` baseline volume controls
- common ioctl subset sufficient for broad portable software

## Translation strategy

- OSS ioctls translated to native `AUDIO_IOC_*` operations.
- OSS semantic differences are normalized with documented behavior.
- Unsupported OSS calls fail deterministically and are listed explicitly.

## OSS ioctl translation matrix (initial)

| OSS ioctl | Expected behavior | Native mapping | Notes |
|----------|-------------------|----------------|-------|
| `SNDCTL_DSP_RESET` | reset stream, drop queued samples | `AUDIO_IOC_DROP` + reset pointers | leaves fd open |
| `SNDCTL_DSP_SYNC` | block until playback drains | `AUDIO_IOC_DRAIN` | blocking behavior required |
| `SNDCTL_DSP_SPEED` | set/get sample rate | `AUDIO_IOC_SET_PARAMS` / `GET_PARAMS` | return actual negotiated rate |
| `SNDCTL_DSP_CHANNELS` | set/get channel count | `AUDIO_IOC_SET_PARAMS` / `GET_PARAMS` | return actual channel count |
| `SNDCTL_DSP_SETFMT` | set/get sample format | `AUDIO_IOC_SET_PARAMS` / `GET_PARAMS` | map OSS AFMT constants |
| `SNDCTL_DSP_GETFMTS` | bitmask of supported formats | `AUDIO_IOC_QUERY_CAPS` | derive OSS AFMT mask |
| `SNDCTL_DSP_GETBLKSIZE` | preferred block size | params period bytes | expose period_bytes |
| `SNDCTL_DSP_NONBLOCK` | set nonblocking mode | fd flags (`O_NONBLOCK`) | keep standard fd semantics |
| `SNDCTL_DSP_GETOSPACE` | output buffer availability | `AUDIO_IOC_GET_STATUS` | convert frames->bytes/frags |
| `SNDCTL_DSP_GETODELAY` | queued output delay | `AUDIO_IOC_GET_STATUS.delay_frames` | convert to bytes |
| `SNDCTL_DSP_SETFRAGMENT` | request fragment geometry | params period/periods negotiation | clamp to backend limits |
| `SNDCTL_DSP_POST` | start output after setup | `AUDIO_IOC_START` | no-op if already running |
| `SOUND_MIXER_READ_VOLUME` | read master volume | policy via audioctl/daemon or native master control | return L/R packed OSS format |
| `SOUND_MIXER_WRITE_VOLUME` | set master volume | policy write to master gain | clamp and return actual |
| `SOUND_MIXER_READ_PCM` | read pcm stream volume | stream/sink gain read | semantics documented |
| `SOUND_MIXER_WRITE_PCM` | set pcm stream volume | stream/sink gain write | semantics documented |

## OSS open/write behavior

- Open `/dev/dsp` returns playback-capable fd.
- First write may implicitly prepare/start if app does not issue explicit trigger ioctls.
- Blocking semantics mirror native write behavior.
- Nonblocking writes return `-EAGAIN` when no space.

## OSS format mapping table

| OSS AFMT | Native format |
|----------|---------------|
| `AFMT_U8` | `AUDIO_FMT_U8` |
| `AFMT_S16_LE` | `AUDIO_FMT_S16_LE` |
| `AFMT_S32_LE` | `AUDIO_FMT_S32_LE` |
| `AFMT_S24_LE` (if defined in OSS layer) | `AUDIO_FMT_S24_LE` |

Unsupported AFMT values must return deterministic unsupported errors.

---

## 11) Compatibility Edge-Case Policy

1. If OSS requests unsupported fragment geometry:
- clamp to nearest supported values
- report actual values to caller

2. If app changes rate/channels/format while running:
- stop stream safely
- apply `SET_PARAMS`
- require explicit or implicit restart policy (documented and consistent)

3. If xrun occurs in OSS mode:
- return `-EPIPE` on write path
- allow recovery via reset/sync semantics

4. Mixer behavior in audiod mode:
- `/dev/mixer` controls daemon-exposed master/sink/pcm policy values
- if direct-hw mode active, behavior should remain deterministic and documented

---

## 12) Stage 0 Deliverables Checklist

1. Add `include/audio.h` and `include/audio_ioctl.h` containing finalized contract types and constants.
2. Add kernel validation helpers for:
- struct size/version checks
- enum/range checks
- state-transition checks
3. Add initial `/proc/audio` output fields matching status contract names.
4. Add ABI contract tests (size/version compatibility checks) in userland test utility.
5. Add OSS mapping spec file references in code comments where translation layer will land.

---

## 13) Out-of-Scope for Stage 0 Contract Freeze

1. Capture data path semantics beyond reserved fields.
2. mmap ring-buffer ABI.
3. Full PulseAudio protocol compatibility.
4. Full ALSA kernel ABI emulation.

These remain later-stage tasks per `docs/audio-subsystem-implementation-plan.md`.

---

## 14) Change Control

Any changes to this contract require:

1. Explicit document update in this file.
2. Matching updates in `docs/audio-subsystem-design.md` or `docs/audio-subsystem-implementation-plan.md` if behavior/policy changes.
3. ABI compatibility note in commit message and roadmap notes when user-visible behavior changes.
