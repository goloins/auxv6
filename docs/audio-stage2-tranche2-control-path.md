# Audio Stage 2 Tranche 2 Control Path Notes

## Status

Date: 2026-04-05

This document captures Stage 2 Tranche 2: a minimal live control path for `audiod` plus a helper utility (`audiodctl`).

Related docs:

- docs/audio-stage2-tranche1-daemon-scaffold.md
- docs/audio-subsystem-implementation-plan.md
- docs/audio-subsystem-design.md

## Why this tranche exists

Tranche 1 established continuous sink servicing but required daemon restart for parameter changes or runtime introspection.

Tranche 2 adds a lightweight control channel so stream behavior can be adjusted while `audiod` stays alive.

## Files changed

Userland:

- user/audiod.c
- user/audiodctl.c
- targetfs/usr/share/man/audiod.md
- targetfs/usr/share/man/audiodctl.md

Build and packaging:

- Makefile
- .gitignore

Docs:

- docs/audio-subsystem-design.md
- docs/audio-subsystem-implementation-plan.md
- docs/ROADMAP.md
- docs/CHANGELOG-2026-04.md
- docs/man-pages.md
- docs/audio-stage2-tranche2-control-path.md (this document)

## Control channel behavior

Default control path:

- `/tmp/audiod.ctl`

Model:

1. `audiodctl` writes a one-shot command file.
2. `audiod` polls for that file during its loop.
3. When present, `audiod` reads command text, applies it, and unlinks the file.

Supported commands:

1. `status`
2. `set <rate> <channels> <format> <period_frames> <periods> <buffer_frames>`
3. `set-write <bytes>`
4. `set-timeout <ms>`

`set` command application path:

- `STOP` -> `SET_PARAMS` -> `PREPARE` -> `START`

## Manual smoke sequence

Inside guest:

1. `audiod -f -v`
2. `audiodctl status`
3. `audiodctl set-write 1024`
4. `audiodctl set-timeout 100`
5. `audiodctl set 48000 2 0 256 4 1024`

Expected high-level signals:

1. audiod logs command acceptance.
2. status output reflects live run counters/state.
3. stream reconfigure applies without daemon restart.

## Limitations

1. Single command mailbox file (no queue semantics).
2. Last writer wins if commands race.
3. No auth/permissions policy yet beyond filesystem ownership.
4. Control path is local-file based, not a structured IPC protocol.

## Next Stage 2 recommendation

1. Add a dedicated long-lived control endpoint (for example, command socket/fd channel) with queued request semantics.
2. Add structured status dump command suitable for machine parsing.
3. Expand audiod toward multi-stream policy/mix loop.
