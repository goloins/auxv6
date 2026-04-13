# cowtest(1)

## Name
cowtest - COW fork correctness test for parent/child write isolation

## Synopsis
cowtest [-r rounds]

## Description
`cowtest` validates that `fork()` behaves as a correct copy-on-write
duplication boundary for writable user memory.

Each round initializes three writable regions with the same base pattern:

1. a static data segment region
2. a heap region allocated with `sbrk()`
3. a stack region in the test process

The test then forks and runs a synchronized two-sided isolation check:

1. the child verifies the inherited base contents
2. the child overwrites all three regions with a child-only pattern
3. the parent verifies it still sees the original base pattern
4. the parent overwrites all three regions with a parent-only pattern
5. the child verifies it still sees the child-only pattern
6. after the child exits, the parent verifies it still sees the parent-only pattern

Any mismatch indicates cross-corruption or incomplete copy-on-write handling.

## Options
- `-r rounds`
  Number of synchronized fork/isolation rounds to run. Default: `8`.
- `-h`, `--help`
  Show usage.

## Examples
Run the default COW correctness probe:

```sh
cowtest
```

Run a longer isolation sweep:

```sh
cowtest -r 32
```

## Notes
- This test is aimed at functional VM correctness, not performance.
- Coverage spans writable data, heap, and stack memory so the fork/COW path is
  exercised across the main user-memory classes expected to diverge after
  either side writes.