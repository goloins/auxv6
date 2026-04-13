# cowexectest(1)

## Name
cowexectest - fork-plus-exec COW correctness test for address-space handoff

## Synopsis
cowexectest [-r rounds]

## Description
`cowexectest` validates the correctness boundary between `fork()` copy-on-write
duplication and the subsequent `exec()` address-space replacement.

Each round initializes writable data, heap, and stack regions in the parent,
forks, and then performs this sequence:

1. the child verifies the inherited base contents
2. the child overwrites its old-image data, heap, and stack with a child-only pattern
3. the parent verifies it still sees the original base pattern
4. the child `exec()`s a fresh `cowexectest --exec-child` image
5. the parent overwrites its own regions with a parent-only pattern
6. the exec'd child touches fresh data, heap, and stack in the new image and reports success
7. the parent verifies its parent-only pattern still holds after the child exits

Any mismatch indicates cross-corruption, broken old-image teardown, or an
incorrect fork-to-exec address-space handoff.

## Options
- `-r rounds`
  Number of fork-plus-exec validation rounds to run. Default: `8`.
- `-h`, `--help`
  Show usage.

## Examples
Run the default fork-plus-exec correctness probe:

```sh
cowexectest
```

Run a longer sweep:

```sh
cowexectest -r 24
```

## Notes
- This test targets correctness, not performance.
- The exec helper re-execs the same binary so the new user image is exercised
  without relying on shell path lookup.