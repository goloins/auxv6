# zombie(1)

## Name
zombie - Create a zombie process for testing.

## Synopsis
```
zombie
```

## Duty
Fork a child process that exits immediately, leaving it in zombie state
while the parent sleeps. Used to verify that the kernel correctly handles
zombie reaping and process reparenting.

## Options
None.

## Behavior
1. Forks a child that calls `exit(0)` immediately.
2. The parent sleeps for a short period.
3. While the parent sleeps, the child appears as a zombie in `ps`.
4. The parent eventually calls `wait` and reaps the child.

## Notes
- Run `ps` in another terminal while `zombie` is sleeping to observe the
  zombie state entry.

## Examples
```
zombie &
ps
```

## Source Audit
- Source file: user/zombie.c
- Last updated: 2026-04-02
