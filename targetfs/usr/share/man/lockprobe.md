# lockprobe

## Name
`lockprobe` - stress utility for lock-path validation in console and file-table code

## Synopsis
`lockprobe [-v] [-D] [-l loops] [-w workers] [-r timed_reads] [-C] [-F]`

## Description
`lockprobe` is a focused validation utility for recently modernized locking paths.
It exercises the lock domains that changed during the spinlock/sleeplock split
work:

- File-table (`ftable`) open/read/dup/close paths, including concurrent workers.
- Console write and tty ioctl paths (`tcgetattr`, `tcsetattr`, `TIOCGWINSZ`).
- Console timed read path in noncanonical mode (`VMIN=0`, `VTIME=1`) to hit
  `consoleread` timeout and wakeup behavior.

The utility exits with status `0` on full pass, non-zero if any subtest fails.

## Options
- `-v`: Verbose progress logs.
- `-D`: Debug logs (very chatty). Implies verbose behavior.
- `-l loops`: Loop count for fd/ioctl/write stress. Default: `200`.
- `-w workers`: Worker process count for concurrent file-table stress. Default: `4`.
- `-r timed_reads`: Number of timed tty reads in noncanonical mode. Default: `32`.
- `-C`: Skip console tests.
- `-F`: Skip file-table tests.
- `-h`, `--help`: Print usage.

## Examples
Run full suite with defaults:

```sh
lockprobe
```

Run verbose with stronger concurrency:

```sh
lockprobe -v -l 400 -w 8 -r 64
```

Debug only console paths:

```sh
lockprobe -D -F
```

Debug only file-table paths:

```sh
lockprobe -D -C
```

## Notes
- The timed-read test only runs when stdin is a tty.
- The utility touches `/tmp/lockprobe.tmp` and attempts cleanup.
- For lock panic investigation, keep kernel lock diagnostics enabled
  (`KDEBUG_SPINLOCK_LOCKFAIL=1`).
