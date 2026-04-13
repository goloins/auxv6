# stackgrowtest(1)

## Name
stackgrowtest - On-demand user stack growth regression test.

## Synopsis
```
stackgrowtest [-v] [-d]
```

## Description
`stackgrowtest` exercises the kernel's demand-stack-growth mechanism.  When a
user process touches the guard page below its current stack bottom, the kernel
maps one more page of the pre-allocated stack region as accessible and resumes
execution rather than delivering SIGSEGV.  Large stacks grow incrementally up
to `USER_STACK_MAX_PAGES` pages.

## Options
- `-v` — Verbose mode: print per-test progress (depth reached, frame size).
- `-d` — Debug mode: include stack pointer/depth telemetry and child
   wait-status decoding for overflow diagnostics (implies `-v`).

## Tests Performed

1. **Deep recursion** — Recurses to depth 400 with ~512 bytes of local
   data per frame (~200 KiB total), well beyond the 4-page (16 KiB) initial
   stack budget.  Each new guard-page fault triggers one page of growth.
   **PASS** if the target depth is reached without SIGSEGV.

2. **Fork inheritance** — Grows the stack in the parent process, then forks
   a child that itself recurses deeply.  Verifies that `stack_top`/`stack_bot`
   fields are correctly inherited across `fork(2)` so the child's page-fault
   handler knows what to grow.

3. **Max limit enforcement** — Runs inside a child process that installs a
   SIGSEGV handler and attempts to exceed `USER_STACK_MAX_PAGES`.  **PASS** if
   the child is terminated by SIGSEGV before completing the recursion (limit is
   enforced by the kernel). In debug output this usually appears as
   `child signaled sig=11 raw=0xb`.

## Exit Status
- `0` — all tests passed.
- `1` — one or more tests failed.

## Implementation Notes
At `exec(2)` time the kernel pre-allocates `USER_STACK_GUARD_PAGES +
USER_STACK_MAX_PAGES` pages contiguously.  Only the initial `USER_STACK_PAGES`
are marked user-accessible; the remaining pages are present but inaccessible
(no `PTE_U` flag).  On each guard-page fault `proc_try_grow_stack()` calls
`setpteu()` to promote the guard page into live stack space, slides the guard
pointer one page lower, and refreshes the TLB.  No `kalloc()` is needed during
fault handling.

If SIGSEGV is catchable but the kernel cannot build a signal frame at full
stack exhaustion, auxv6 falls back to fatal signaled termination (SIGSEGV)
instead of reporting a normal exit.

Relevant kernel constants (`include/param.h`):
- `USER_STACK_PAGES` — initial usable pages (default 4 = 16 KiB).
- `USER_STACK_MAX_PAGES` — hard ceiling for growth (default 64 = 256 KiB).
- `USER_STACK_GUARD_PAGES` — permanent guard depth (default 1 page).

## See Also
`sigtest(1)`, `usertests(1)`

## Source Audit
- Source file: `user/stackgrowtest.c`
- Last updated: 2026-04-04 (debug mode and max-test hardening)
