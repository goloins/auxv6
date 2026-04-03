# time(1)

## Name
time - Time a command's execution.

## Synopsis
```
time command [args...]
```

## Duty
Run `command` with optional arguments, measure how long it takes, and
print the elapsed real time.

## Options
None.

## Arguments
- `command` — The program to execute.
- `args...` — Arguments to pass to `command`.

## Output
After the command exits, prints:
```
real  N.MMM
```
Where `N.MMM` is the elapsed time in seconds and milliseconds.

Elapsed time is measured with the kernel monotonic clock. The clock is
anchored by the periodic timer interrupt and interpolated between ticks.

## Examples
```
time ls /
time usertests
time dd if=/dev/zero of=/tmp/t bs=512 count=1000
```

## Source Audit
- Source file: user/time.c
- Last updated: 2026-04-02
