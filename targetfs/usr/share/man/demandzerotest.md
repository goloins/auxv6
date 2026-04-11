# demandzerotest(1)

## Name
demandzerotest - regression test for lazy heap demand-zero fault resolution

## Synopsis
demandzerotest

## Description
`demandzerotest` validates the first real demand-zero use site in the VM fault
dispatcher: lazily provisioned heap growth.

The test grows the heap with `sbrk()` and then exercises three paths:

1. direct user touches of newly grown pages, expecting zero-filled first access
2. pipe reads into untouched lazy user pages, exercising kernel `copyout`
3. pipe writes from untouched lazy user pages, exercising kernel `copyin`

It snapshots `/proc/vmstat` before and after the run and expects:

- `vm_fault_demand_zero` to increase
- `vm_fault_sigsegv` to remain unchanged

## Examples
Run the lazy-heap demand-zero regression:

```sh
demandzerotest
```

## Notes
- This test validates lazy heap materialization and the supporting kernel
  user-copy paths, not performance.