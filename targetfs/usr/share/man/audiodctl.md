# audiodctl(1)

## Name
audiodctl - send runtime control commands to audiod

## Synopsis
```sh
audiodctl [ctl-path] status
audiodctl [ctl-path] set <rate> <channels> <format> <period_frames> <periods> <buffer_frames>
audiodctl [ctl-path] set-write <bytes>
audiodctl [ctl-path] set-timeout <ms>
audiodctl [ctl-path] track-load <slot> <path>
audiodctl [ctl-path] track-loop <slot> <path>
audiodctl [ctl-path] track-stop <slot>
audiodctl [ctl-path] track-gain <slot> <shift>
```

## Description
`audiodctl` writes one-shot control commands to audiod's local control file.

Default control path is `/tmp/audiod.ctl`.

`audiod` consumes and removes the control file during its event loop.

The utility now prints command-queue feedback and warns if no live `audiod`
process is detected when the command is submitted.

## Commands
- `status`: request a runtime status line from audiod.
- `set`: live stream reconfigure (`STOP` + `SET_PARAMS` + `PREPARE` + `START`).
- `set-write`: change write burst size in bytes.
- `set-timeout`: change poll timeout in milliseconds (`-1` allowed).
- `track-load <slot> <path>`: load raw PCM file into mixer slot (0–7), play once.
- `track-loop <slot> <path>`: same but loops indefinitely.
- `track-stop <slot>`: stop and release mixer slot.
- `track-gain <slot> <shift>`: per-slot gain as right-shift (0=full volume, 1=half, max 15).

## Examples
```sh
audiodctl status
audiodctl set 48000 2 0 256 4 1024
audiodctl set-write 1024
audiodctl set-timeout 100
audiodctl track-load 0 /tmp/tone.raw
audiodctl track-loop 1 /tmp/ambient.raw
audiodctl track-gain 1 1
audiodctl track-stop 0
```

## Exit Status
- `0` command file written
- `1` open/argument failure

## See Also
audiod(1), audioctl(1), audiostat(1)

## Source Audit
- Source file: user/audiodctl.c
- Last updated: 2026-04-05
