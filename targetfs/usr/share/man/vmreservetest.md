# vmreservetest

## Name

`vmreservetest` - explicit zero-fill reservation regression test

## Synopsis

```text
vmreservetest
```

## Description

`vmreservetest` validates the first safe demand-zero reland slice.

It exercises an explicit `vmreserve()` reservation instead of lazy `sbrk()` growth and checks:

- untouched reserved pages fault in as zero-filled pages
- non-adjacent first touches work
- writes persist after first touch
- syscall-driven writes can target an untouched reserved page
- fork preserves isolation for materialized pages while untouched reserved pages can still fault in independently

The test is intentionally independent of `/proc/vmstat` so correctness does not depend on procfs availability.

## Exit Status

- `0`: all checks passed
- `1`: one or more checks failed