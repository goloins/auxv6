# audioctl(1)

## Name
audioctl - inspect and control auxv6 Stage-0 audio endpoints

## Synopsis
```sh
audioctl abi [audioctl-dev]
audioctl caps [audioctl-dev]
audioctl default [audioctl-dev]
audioctl set-default <card> <device> <dir> [audioctl-dev]
audioctl params [pcm-dev]
audioctl status [pcm-dev]
audioctl volume [pcm-dev]
audioctl set-volume <left_q8_8> <right_q8_8> <mute> [pcm-dev]
```

## Description
`audioctl` is the administrative utility for the Stage-0 audio ABI.
It issues `ioctl(2)` requests against:

- `/dev/audioctl` for ABI/capability/default-route control
- `/dev/pcmC0D0p` (or another PCM node) for stream params/status/volume

In the current Stage-0 skeleton, values are functional but minimal and intended
for ABI validation and bring-up smoke checks.

## Commands
- `abi`: print kernel audio ABI version fields.
- `caps`: print supported channels/rates/formats and period limits.
- `default`: print current default route tuple `(card, device, dir)`.
- `set-default`: update default route tuple.
- `params`: print current stream parameter state for the PCM endpoint.
- `status`: print stream runtime status and counters.
- `volume`: print stream volume/mute state.
- `set-volume`: set stream left/right dB Q8.8 values and mute flag.

## Examples
```sh
audioctl abi
audioctl caps
audioctl default
audioctl set-default 0 0 0
audioctl params /dev/pcmC0D0p
audioctl status /dev/pcmC0D0p
audioctl set-volume -256 -256 0 /dev/pcmC0D0p
```

## Exit Status
- `0` success
- `1` command failure (open/ioctl/argument error)

## See Also
audiostat(1), ioctl(2)

## Source Audit
- Source file: user/audioctl.c
- Last updated: 2026-04-05
