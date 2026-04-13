# audiotest

## Name
`audiotest` - Stage-0 audio write-path smoke and xrun recovery exerciser

## Synopsis
```sh
audiotest [pcm-dev] [chunks] [chunk-bytes]
```

## Description
`audiotest` opens a PCM playback device, applies a default stream configuration, prepares the stream, and writes synthetic PCM blocks.

It prints periodic status snapshots and attempts recovery if write calls hit xrun state.

## Arguments
- `pcm-dev` (optional): PCM device path. Default: `/dev/pcmC0D0p`.
- `chunks` (optional): Number of write iterations. Default: `128`.
- `chunk-bytes` (optional): Bytes per write call. Default: `512`.

## Exit Status
- `0`: test loop completed.
- `1`: open/ioctl/write/recovery failure.

## Notes
- This is a bring-up utility for the current Stage-0/Stage-1 skeleton; it is not an audio quality test.
- Current implementation writes deterministic synthetic byte patterns rather than decoded audio content.
