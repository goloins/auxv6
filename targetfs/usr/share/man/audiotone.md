# audiotone

## Name
`audiotone` - deterministic raw PCM square-wave generator for audio testing

## Synopsis
```sh
audiotone [-o out.raw] [-f hz] [-d ms] [-r rate] [-c channels] [-a amplitude]
```

## Description
`audiotone` generates a deterministic square-wave PCM stream in `S16_LE` format.
The output is intended for auxv6 audio path tests (for example with `audiod`
track commands).

By default it writes one second of a 440 Hz tone to `/tmp/tone.raw`.

## Options
- `-o out.raw`: output file path. Use `-` for stdout.
- `-f hz`: tone frequency in Hz. Default: `440`.
- `-d ms`: duration in milliseconds. Default: `1000`.
- `-r rate`: sample rate in Hz. Default: `48000`.
- `-c channels`: channel count. Default: `2`.
- `-a amplitude`: peak sample amplitude (`1..32767`). Default: `12000`.

## Examples
```sh
audiotone
audiotone -o /tmp/tone1k.raw -f 1000 -d 500
audiodctl track-loop 0 /tmp/tone1k.raw
```

## Exit Status
- `0`: output written successfully.
- `1`: argument/IO failure.

## See Also
audiod(1), audiodctl(1), audiotest(1)

## Source Audit
- Source file: user/audiotone.c
- Last updated: 2026-04-05
