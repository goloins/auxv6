# forktest(1)

## Name
forktest - Process fork stress test.

## Synopsis
```
forktest
```

## Duty
Stress-test the `fork(2)` system call by repeatedly creating child processes.
Verifies that the kernel can handle high fork rates and correctly limits the
number of simultaneous processes.

## Options
None.

## Behavior
- Forks many children in a tight loop until no more processes can be created.
- Each child exits immediately.
- Verifies the correct number of children were reaped.
- Tests that the process table limit is enforced.

## Examples
```
forktest
```

## Source Audit
- Source file: user/forktest.c
- Last updated: 2026-04-02
