# df(1)

## Name
df - Report filesystem disk usage.

## Synopsis
```
df [-h]
```

## Duty
Display disk space usage for all mounted filesystems by reading
`/proc/mountstats`.

## Options
- `-h` — Human-readable output. Sizes are shown with `K`, `M`, or `G` suffixes
  instead of raw 1 KB block counts.

## Output Columns
- `Filesystem` — Device or filesystem name
- `1K-blocks` (or human-readable) — Total capacity
- `Used` — Space consumed
- `Available` — Free space
- `MountPoint` — Where the filesystem is mounted

## Examples
```
df
df -h
```

## Source Audit
- Source file: user/df.c
- Last updated: 2026-04-02
