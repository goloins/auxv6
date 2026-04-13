# audiod(1)

## Name
audiod - Stage-2 audio daemon scaffold for native PCM sink servicing

## Synopsis
```sh
audiod [-f] [-v] [-d pcm-dev] [-C ctl-path] [-r rate] [-c channels] [-F format]
       [-p period_frames] [-n periods] [-b buffer_frames]
       [-w write_bytes] [-t poll_timeout_ms]
```

## Description
`audiod` is the initial Stage-2 policy-daemon scaffold for auxv6 audio.

Current behavior is intentionally minimal:
- opens and configures one native PCM playback endpoint
- starts the stream
- runs a poll-driven loop that writes silent frames when writable
- performs xrun recovery (`RESET_XRUN` + `PREPARE` + `START`)
- consumes one-shot control commands from a local control file

This tranche establishes daemon lifecycle and steady sink servicing. It does not yet provide multi-client mixing/routing.

## Options
- `-f`: run in foreground (default is daemon mode via double-fork).
- `-v`: verbose status logging.
- `-d pcm-dev`: PCM device path (default `/dev/pcmC0D0p`).
- `-C ctl-path`: control file path (default `/tmp/audiod.ctl`).
- `-r rate`: sample rate (default `48000`).
- `-c channels`: channel count (default `2`).
- `-F format`: sample format enum value (default `0` = `AUDIO_FMT_S16_LE`).
- `-p period_frames`: period size in frames (default `256`).
- `-n periods`: period count (default `4`).
- `-b buffer_frames`: total buffer frames (default `1024`).
- `-w write_bytes`: bytes per write burst (default `512`, max `4096`).
- `-t poll_timeout_ms`: poll timeout in milliseconds (default `250`, `-1` for infinite).

## Examples
```sh
# Foreground bring-up with verbose logs
audiod -f -v

# Daemon mode with explicit stream params
audiod -r 48000 -c 2 -F 0 -p 256 -n 4 -b 1024

# Read commands from custom control mailbox path
audiod -f -v -C /tmp/audiod.ctl
```

## Runtime Control Commands

The daemon checks the control file path during its loop and applies one-shot commands:

- `status`
- `set <rate> <channels> <format> <period_frames> <periods> <buffer_frames>`
- `set-write <bytes>`
- `set-timeout <ms>`
- `track-load <slot> <path>` — load a raw PCM file into mixer slot 0–7 and play once.
- `track-loop <slot> <path>` — same but loops indefinitely.
- `track-stop <slot>` — stop and release a mixer slot.
- `track-gain <slot> <shift>` — set per-slot right-shift gain (0=full, 1=half, … max 15).

## Exit Status
- `0` clean shutdown
- `1` open/configure/poll/write/recovery failure

## See Also
audiodctl(1), audioctl(1), audiostat(1), audiotest(1), poll(2), ioctl(2)

## Source Audit
- Source file: user/audiod.c
- Last updated: 2026-04-05
