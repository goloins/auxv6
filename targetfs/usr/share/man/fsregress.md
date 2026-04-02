# fsregress(1)

## Name
fsregress - Filesystem regression test suite.

## Synopsis
```
fsregress
```

## Duty
Run a basic regression pass against the mounted filesystem. Scans directories,
reads directory entries via `getdents`, and runs `stat` on each entry to verify
metadata consistency.

## Options
None.

## Notes
- Tests are run on the root filesystem and current working directory.
- Print `PASS` or `FAIL` for each check.

## Examples
```
fsregress
```

## Source Audit
- Source file: user/fsregress.c
- Last updated: 2026-04-02
