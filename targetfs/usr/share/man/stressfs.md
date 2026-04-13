# stressfs(1)

## Name
stressfs - Filesystem concurrent I/O stress test.

## Synopsis
```
stressfs
```

## Duty
Stress the filesystem by forking 4 processes that each perform concurrent
file write and read operations on unique temporary files. Tests that
concurrent I/O produces consistent results under load.

## Options
None.

## Behavior
- Forks 4 child processes, each writing and reading its own file.
- Each child writes a repeating character pattern and reads it back.
- Parent waits for all children and reports pass/fail.

## Examples
```
stressfs
```

## Source Audit
- Source file: user/stressfs.c
- Last updated: 2026-04-02
