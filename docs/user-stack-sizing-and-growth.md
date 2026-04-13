# auxv6 User Stack Sizing and Growth Notes

Date: 2026-04-04

## Scope

This document defines the current user-stack policy in auxv6, why it exists,
and how the kernel behaves at growth and overflow boundaries.

## Current Policy

### Constants (`include/param.h`)

- `USER_STACK_GUARD_PAGES = 1`
- `USER_STACK_PAGES = 4`
- `USER_STACK_MAX_PAGES = 64`

With 4 KiB pages:

- Initial usable stack: 16 KiB.
- Maximum usable stack: 256 KiB.
- Reserved stack region including guard: 260 KiB.

### `exec` behavior (`kernel/core/exec.c`)

1. Kernel pre-allocates guard + max stack pages contiguously.
2. Only initial `USER_STACK_PAGES` pages are user-accessible (`PTE_U`).
3. Lower headroom pages are present but inaccessible (`clearpteu`).
4. `struct proc` gets:
   - `stack_top` (fixed at stack region top)
   - `stack_bot` (lowest currently accessible page)

## Growth Behavior

### Page-fault path (`kernel/core/trap.c` + `kernel/core/proc.c`)

On user `T_PGFLT`, kernel attempts stack growth via:

```c
proc_try_grow_stack(p, fault_addr)
```

Growth succeeds only if:

- Fault is in current guard band: `[stack_bot - PGSIZE, stack_bot)`.
- Used pages `< USER_STACK_MAX_PAGES`.

On success:

- Promote one page: `setpteu(pgdir, stack_guard)`.
- Move `stack_bot` down one page.
- Flush TLB with `switchuvm(p)`.
- Retry faulting instruction.

On failure:

- No growth.
- SIGSEGV delivery proceeds.

## Overflow and Signal-Delivery Semantics

When stack is fully exhausted, a process may catch SIGSEGV. Signal delivery
requires pushing a signal frame to user stack. If that frame setup fails
(bounds/copyout), auxv6 now force-terminates as signaled:

- `xstatus = WSTATUS_SIG(signo)`
- `killed = 1`

This prevents false normal-exit status and aligns with expected Unix fallback
behavior in the absence of alternate signal stacks.

## Fork Semantics

`fork()` copies `stack_top` and `stack_bot`, so the child inherits the same
current stack-growth position and remaining growth window.

Fork behavior (modernized):

- Child address spaces are now copied sparsely: `copyuvm()` skips non-user
   (`!PTE_U`) mappings, including stack reserve/guard pages.
- Stack-growth faults in children allocate one page on demand when the next
   guard-band page is absent, preserving one-page-at-a-time growth semantics.

## Memory Cost

Per process reserved stack region:

$$
(\text{USER\_STACK\_GUARD\_PAGES} + \text{USER\_STACK\_MAX\_PAGES}) \times \text{PGSIZE}
= 65 \times 4096 = 266240\;\text{bytes}
$$

That is 260 KiB per process.

Worst case with `NPROC=128`:

$$
128 \times 260\;\text{KiB} = 33280\;\text{KiB} \approx 32.5\;\text{MiB}
$$

On a 512 MiB physical ceiling, this is about 6.4%.

## Regression Coverage

Primary test utility: `stackgrowtest`.

Validated cases:

1. Deep recursion grows stack incrementally beyond initial 16 KiB.
2. Fork child inherits growth metadata and recurses successfully.
3. Exceeding max stack yields SIGSEGV signaled termination (`sig=11`).

Manual guest validation currently reports 3/3 passing.

## Operational Guidance for Userland

- Avoid large stack-local buffers when heap/static storage is acceptable.
- Keep recursion bounded unless intentional.
- Prefer iterative algorithms in utilities when practical.
- Use `stackgrowtest -d` to inspect depth/SP behavior when diagnosing growth.
