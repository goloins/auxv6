# usertests(1)

## Name
usertests - Comprehensive filesystem and syscall regression tests.

## Synopsis
```
usertests
```

## Duty
Run a comprehensive regression suite covering filesystem operations and
system call correctness. Tests many aspects of inode management, directory
handling, file I/O, symbolic links, and concurrent access.

## Options
None.

## Tests Performed (selection)
- Inode create/unlink, link counts, hard links
- Directory creation, removal, rename
- Concurrent file I/O from multiple processes
- Read/write of large files
- Symbolic link creation and following
- Path resolution edge cases (`.`, `..`, long paths)
- `stat`, `fstat`, `open` with various flags
- `fork`, `exec`, `wait` correctness

## Notes
- Designed to be run in the auxv6 guest environment.
- Test output shows `PASSED` or `FAILED` for each sub-test.
- Some tests create temporary files under `/` or the current directory;
  run from a writable filesystem.

## Examples
```
usertests
```

## Source Audit
- Source file: user/usertests.c
- Last updated: 2026-04-02
