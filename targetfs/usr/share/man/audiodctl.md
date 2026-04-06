# audiodctl(1)

## Name
audiodctl - send runtime control commands to audiod

## Synopsis
```sh
audiodctl [ctl-path] status
audiodctl [ctl-path] set <rate> <channels> <format> <period_frames> <periods> <buffer_frames>
audiodctl [ctl-path] set-write <bytes>
audiodctl [ctl-path] set-timeout <ms>
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

## Examples
```sh
audiodctl status
audiodctl set 48000 2 0 256 4 1024
audiodctl set-write 1024
audiodctl set-timeout 100
```

## Exit Status
- `0` command file written
- `1` open/argument failure

## See Also
audiod(1), audioctl(1), audiostat(1)

## Source Audit
- Source file: user/audiodctl.c
- Last updated: 2026-04-05
