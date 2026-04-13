# audiostat(1)

## Name
audiostat - display Stage-1 audio procfs summary, counters, and active stream table

## Synopsis
```sh
audiostat
```

## Description
`audiostat` reads and prints audio procfs nodes:

- `/proc/audio`
- `/proc/audio_stats`
- `/proc/audio_clients`

This is a lightweight observability helper for early audio bring-up.

## Output
The utility prints each source path followed by its current text snapshot.
Exact keys are kernel-defined and may expand as the subsystem matures.

## Examples
```sh
audiostat
```

## Exit Status
- `0` success
- `1` one or more procfs reads failed

## See Also
audioctl(1), procfs(5)

## Source Audit
- Source file: user/audiostat.c
- Last updated: 2026-04-05
