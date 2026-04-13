# symlinktest(1)

## Name
symlinktest - Symbolic link regression test suite.

## Synopsis
```
symlinktest
```

## Duty
Run a series of regression tests for symbolic link support. Verifies
creation, traversal, chained links, and broken link handling.

## Options
None.

## Tests Performed
1. **Create** — Creates a symlink and verifies `readlink` returns the target.
2. **Follow** — Opens a file through a symlink.
3. **Chain** — Creates a chain of symlinks and verifies the final target is
   reached correctly.
4. **Broken link** — Verifies `open` of a broken symlink fails with `ENOENT`.
5. **Cleanup** — Removes all test symlinks.

## Examples
```
symlinktest
```

## Source Audit
- Source file: user/symlinktest.c
- Last updated: 2026-04-02
