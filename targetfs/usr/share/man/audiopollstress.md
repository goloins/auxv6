# audiopollstress

## Name
`audiopollstress` - concurrent audio stream poll/write stress tool

## Synopsis
```sh
audiopollstress [options] [pcm-dev]
```

## Description
`audiopollstress` opens N PCM playback stream descriptors simultaneously,
then drives a single `poll(2)` loop over all of them.  Each time a
descriptor signals `POLLOUT` the tool writes a fixed-size chunk of
synthetic PCM data.  When a descriptor signals `POLLERR` (xrun state)
an automatic recovery sequence (`AUDIO_IOC_RESET_XRUN` +
`AUDIO_IOC_PREPARE`) is attempted.

The tool is designed to exercise the kernel-side audio poll-wakeup path
under concurrent write pressure and to validate that readiness signals
arrive coherently across multiple independent stream fds that share the
same hardware-clock ring.

With `-N` each stream fd is put into non-blocking mode via
`fcntl(F_SETFL, O_NONBLOCK)` after configuration, exercising the
`audio_set_nonblock` / `F_GETFL` path added in the Stage-1 tranche-3
follow-on.

## Options
| Option | Description |
|--------|-------------|
| `-n <streams>` | Number of concurrent PCM stream fds (default: 4, max: 16) |
| `-c <chunks>`  | Total write chunks to deliver per stream (default: 64) |
| `-b <bytes>`   | Bytes per write chunk (default: 512, max: 1024) |
| `-t <ms>`      | `poll()` timeout in milliseconds (default: 1000; use -1 for infinite) |
| `-N`           | Toggle `O_NONBLOCK` via `fcntl(F_SETFL)` after stream configuration |
| `-v`           | Verbose: print per-event lines as poll rounds complete |

`pcm-dev` defaults to `/dev/pcmC0D0p`.

## Output
Each stream fd is configured with:
- 48 kHz, 2-channel, S16_LE
- 256-frame periods, 4 periods, 1024-frame buffer

After all chunks are delivered (or streams fail), a summary table is
printed:

```
stream chunks   pollhit  pollerr  writes   werr     xruns    result
0      64       69       0        64       0        0        PASS
1      64       71       0        64       0        0        PASS
...

Result: PASS
```

Columns:
- **chunks** — number of chunks confirmed written for this stream
- **pollhit** — total `POLLOUT` events received from `poll()`
- **pollerr** — total `POLLERR` events (xrun notifications)
- **writes** — successful `write()` calls
- **werr** — `write()` calls that returned `-1`
- **xruns** — streams that entered xrun state (POLLERR count)
- **result** — `PASS` if all chunks delivered and no write errors

## Exit Status
- `0` — all streams passed
- `1` — one or more streams failed or hit an unrecoverable error

## Notes
- This is a Stage-1 audio subsystem validation tool; it does not test
  hardware audio output quality.
- The write buffer contains a deterministic incrementing byte pattern
  (`0x00, 0x01, …, 0xFF, 0x00, …`).
- Use `-v` together with a small `-c` value for detailed tracing.
- The tool calls `AUDIO_IOC_DRAIN` once a stream has delivered all its
  chunks, so the kernel-side stream drains before the fd is closed.

## See Also
`audiotest(1)`, `audiostat(1)`, `audioctl(1)`
