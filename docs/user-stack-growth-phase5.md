# User Stack Growth on Demand — Phase 5

## Overview

Status: Complete (2026-04-04)
Goal: Allow user stacks to grow on demand up to a fixed ceiling, instead of
failing immediately at the initial stack size.

This phase is now implemented in kernel and userland, with regression coverage
via `stackgrowtest`.

## Implemented Design

### Stack policy constants (`include/param.h`)

```c
#define USER_STACK_GUARD_PAGES  1
#define USER_STACK_PAGES        4
#define USER_STACK_MAX_PAGES   64
```

- Initial usable stack: 4 pages (16 KiB).
- Maximum usable stack: 64 pages (256 KiB).
- Permanent guard: 1 page below the maximum growth window.

### Per-process metadata (`include/proc.h`)

```c
uint stack_top;   /* VA of top of stack region (set at exec) */
uint stack_bot;   /* VA of lowest currently accessible stack page */
```

### `exec` initialization (`kernel/core/exec.c`)

At exec time, the kernel pre-allocates the full stack region:

- `USER_STACK_GUARD_PAGES + USER_STACK_MAX_PAGES` pages are allocated.
- Only the top `USER_STACK_PAGES` pages are user-accessible (`PTE_U`).
- Guard + headroom pages are mapped but inaccessible (`clearpteu`).

This keeps page-fault growth simple and deterministic (no `kalloc()` in fault
path).

### Growth on page fault (`kernel/core/proc.c`, `kernel/core/trap.c`)

`trap()` handles `T_PGFLT` and calls:

```c
int proc_try_grow_stack(struct proc *p, uint fault_addr)
```

Growth succeeds only when:

1. Fault is in current guard band `[stack_bot - PGSIZE, stack_bot)`.
2. Current used pages are `< USER_STACK_MAX_PAGES`.

On success:

- `setpteu(p->pgdir, stack_guard)` makes one page user-accessible.
- `p->stack_bot` moves down by one page.
- `switchuvm(p)` flushes TLB.

On failure:

- No growth.
- Normal SIGSEGV path is used.

### Signal fallback at hard stack exhaustion (`kernel/core/proc.c`)

If SIGSEGV is caught but signal-frame setup fails due to exhausted stack
(bounds/copyout failure), kernel now forces fatal signaled termination:

- `p->xstatus = WSTATUS_SIG(signo)`
- `p->killed = 1`

This avoids incorrect normal-exit (`status=0`) outcomes and matches expected
Unix behavior when no alternate signal stack is available.

### Fork propagation (`kernel/core/proc.c`)

`fork()` copies `stack_top` and `stack_bot` so child growth state is inherited.

### VM helper (`kernel/core/vm.c`)

Added `setpteu()` (inverse of `clearpteu()`) for stack growth promotion.

## Regression Test Utility

### User tool (`user/stackgrowtest.c`)

Covers three cases:

1. `test_deep_recursion`
- Recurses beyond initial 16 KiB stack; verifies incremental growth works.

2. `test_fork_inherit`
- Grows in parent, then validates deep recursion in child.

3. `test_max_exceeded`
- Exceeds max stack and verifies process termination by SIGSEGV.

Debug options:

- `-v`: verbose test progress.
- `-d`: depth/stack-pointer telemetry + wait status decode (`-v` implied).

Build/install integration:

- Make target `_stackgrowtest` added.
- Included in `UPROGS` for rootfs staging.
- Binary path ignored in `.gitignore` via wildcard policy.
- Man page: `targetfs/usr/share/man/stackgrowtest.md`.

## Validation Transcript (Guest)

Validated manually in guest:

```text
root@auxv6.local:~# stackgrowtest
stackgrowtest: USER_STACK_PAGES=4 USER_STACK_MAX_PAGES=64
PASS test_deep_recursion: reached depth 400
PASS test_fork_inherit: child recursion succeeded
PASS test_max_exceeded: child terminated on stack overflow (exit=11)
stackgrowtest: 3/3 tests passed
```

And with diagnostics:

```text
root@auxv6.local:~# stackgrowtest -d
...
[d] child signaled sig=11 raw=0xb
PASS test_max_exceeded: child terminated on stack overflow (exit=11)
stackgrowtest: 3/3 tests passed
```

## Files Changed in This Phase

- `include/param.h`
- `include/proc.h`
- `include/defs.h`
- `kernel/core/exec.c`
- `kernel/core/proc.c`
- `kernel/core/trap.c`
- `kernel/core/vm.c`
- `user/stackgrowtest.c`
- `targetfs/usr/share/man/stackgrowtest.md`
- `docs/user-stack-sizing-and-growth.md`
- `docs/DEBUG-FLAGS.md`
- `docs/ROADMAP.md`
- `Makefile`
- `.gitignore`
