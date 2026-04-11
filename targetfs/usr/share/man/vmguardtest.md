# vmguardtest(1)

## Name
vmguardtest - regression test for VM address-space guard bypass leakage

## Synopsis
vmguardtest [-p procfs_iters] [-P pipe_iters] [-f fork_iters]

## Description
`vmguardtest` checks that ordinary current-process user-copy paths do not
accidentally drive `vm_as_guard_bypass_no_as` or other fallback-only guard
telemetry.

It snapshots `/proc/vmstat`, runs three focused phases, and checks the guard
counter deltas after each phase:

1. repeated current-process procfs reads from `/proc/meminfo`
2. repeated pipe write/read round trips with user buffers
3. repeated forked-child procfs reads followed by `waitpid`

For each phase, the test expects:

- `vm_as_guard_checks` to increase
- `vm_as_guard_denies` to remain unchanged
- `vm_as_guard_bypass_vm_size` to remain unchanged
- `vm_as_guard_bypass_no_as` to remain unchanged

If any phase increases a bypass or deny counter, `vmguardtest` exits nonzero.

## Options
- `-p procfs_iters`
  Number of `/proc/meminfo` reads in the current-process phase. Default: `64`.
- `-P pipe_iters`
  Number of pipe round trips in the current-process phase. Default: `64`.
- `-f fork_iters`
  Number of forked-child procfs reads. Default: `32`.
- `-h`, `--help`
  Show usage.

## Examples
Run the default guard regression probe:

```sh
vmguardtest
```

Increase fork pressure while keeping the same current-process phases:

```sh
vmguardtest -f 96
```

## Notes
- The test is intended for VM migration validation, especially around the
  `vm_as_guard_*` `/proc/vmstat` counters.
- The post-phase `/proc/vmstat` snapshots are part of the measurement path, so
  the test checks for zero bypass and deny deltas rather than trying to keep
  `vm_as_guard_checks` flat.