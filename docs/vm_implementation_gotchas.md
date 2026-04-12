# VM Implementation Gotchas

Date: 2026-04-11

This note records the specific failure modes from the reverted lazy-heap attempt and the rules for relanding demand-zero safely.

## What Went Wrong In The Reverted Slice

The reverted implementation changed too many contracts at once.

- `growproc()` stopped meaning "new heap bytes are backed now" and started meaning "new heap bytes are only reserved now".
- `copyin()` and `copyout()` were widened to instantiate absent pages, so user memory could appear through syscall-side buffer handling instead of only through the page-fault path.
- the fault dispatcher stopped being a narrow router and became the center of a larger semantic rewrite that also touched heap growth, user-copy, procfs counters, and test scaffolding.
- procfs observability became part of correctness bring-up, so when VM changes regressed `/proc/vmstat`, diagnostics and functionality failed together.
- rollback then became harder because the reverted feature had spread into VMA metadata, fault logic, user-copy paths, stats export, test binaries, and boot/test plumbing.

The root lesson is simple: demand-zero is fine, transparent laziness across the whole process ABI is not a safe first landing.

## Rules For The Reland

The revised plan keeps the scope narrow and auditable.

1. Do not change `sbrk()` or `growproc()` semantics in the first reland.
2. Add one explicit reservation API instead of making normal heap growth lazy.
3. Instantiate pages only from the page-fault path.
4. Do not make `copyin()` or `copyout()` allocate absent user pages.
5. Do not make procfs counters part of the initial correctness story.
6. Use one dedicated VMA type for fault-only zero-fill reservations.
7. Validate memory semantics first, then add observability later.

## Revised Reland Plan

### Phase A: Explicit Reservation Only

Add a new syscall, `vmreserve(nbytes)`, that returns a page-aligned user virtual address range reserved for future fault-time materialization.

Constraints:

- it is explicit, so existing heap users are unaffected
- it rounds start and length to whole pages
- it extends process size so the range is part of the legal user VA envelope
- it records the reservation in VMA metadata as a dedicated zero-fill range
- it does not install PTEs up front

### Phase B: Fault-Only Demand-Zero

Teach the page-fault dispatcher to resolve one additional case:

- user fault
- not-present page
- address covered by a zero-fill reservation VMA

Resolution:

- allocate one physical page
- zero it
- map it user-accessible
- return to user mode

Non-goals for this phase:

- no copyin/copyout materialization
- no implicit heap laziness
- no file-backed demand paging
- no procfs stats dependency

### Phase C: Direct Regression Coverage

Use a dedicated test binary that proves:

- untouched reserved pages read back as zero
- sparse first-touch works across non-adjacent pages
- writes persist after first touch
- fork preserves isolation for materialized pages
- untouched reserved pages can still fault in after fork

### Phase D: Only Then Consider Broader Semantics

After the isolated path is stable, audit every place that assumes a successful heap growth already implies mapped user pages before considering any lazy `growproc()` design.

That audit must include:

- syscall argument fetch paths
- signal frame setup
- exec stack/image setup
- kernel writes into user buffers
- fork inheritance behavior for absent-but-reserved pages

## Current First Reland Slice

The first implementation slice lands only:

- explicit `vmreserve()`
- a dedicated `VMA_ZEROFILL` reservation type
- fault-only zero-fill instantiation
- a narrow user regression test that does not depend on procfs

If that slice fails, the rollback surface is small and local.

## Validation Status

Guest validation for the first reland slice succeeded on 2026-04-12.

Observed manual run:

```text
root@auxv6.local:~# vmreservetest
vmreservetest: profile=2026-04-11-r1 reserve=12288 base=0x0006a000
[PASS] first touch zero fill
[PASS] write persistence
[PASS] fork isolation
vmreservetest: passed=3 failed=0
```

Observed `/proc/vmstat` immediately after the test:

```text
vm_fault_dispatches 66
vm_fault_cow_resolved 61
vm_fault_stack_growth 1
vm_fault_demand_zero 4
vm_fault_sigsegv 0
```

That result validates the intended narrow contract:

- explicit reservation works
- zero-fill happens at fault time
- sparse first touch works
- fork preserves isolation for touched pages without requiring lazy heap semantics
- demand-zero fault accounting is wired correctly into `/proc/vmstat`

The next additive step is observability only: export a demand-zero counter through `/proc/vmstat` without changing `copyin()`, `copyout()`, or `growproc()` semantics.

Update 2026-04-12, later:

- a second narrow slice is now landed: `copyout()` may materialize absent pages only for explicit `VMA_ZEROFILL` reservations
- this is intentionally not a general lazy-heap feature
- `copyin()` still requires present source pages, so transparent lazy heap semantics are still not in place

Guest validation for that second slice also succeeded on 2026-04-12.

Observed result:

```text
vmreservetest: passed=4 failed=0
```

That confirms the new syscall-output case works as intended: a kernel write path can target an untouched explicit reservation without broadening heap semantics or enabling general copyin-side materialization.