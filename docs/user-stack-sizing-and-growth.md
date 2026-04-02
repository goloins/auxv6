# auxv6 User Stack Sizing and Growth Notes

Date: 2026-04-02

## Why this note exists

We hit a real userland bug where `cp` produced empty output because a large
stack allocation in `cp.c` exhausted the process stack budget.

This note records the current behavior, how it differs from mainstream Unix,
and what to implement next so we do not keep rediscovering the same issue.

## Current auxv6 behavior

Current `exec` stack policy is effectively:

- Allocate two pages after program image alignment.
- Mark the lower page inaccessible as a guard page.
- Use the upper page as the entire user stack.

In code:

- `kernel/core/exec.c`: `allocuvm(..., sz + 2*PGSIZE)`, `clearpteu(...)`, `sp = sz`.
- `include/mmu.h`: `PGSIZE == 4096`.

So current user stack budget is one page (4 KiB), minus argv/setup overhead.

## Comparison to mainstream Unix

Mainstream Unix-like systems generally provide multi-megabyte default stacks
for user processes/threads (often configurable via rlimit).

auxv6 is intentionally leaner, but one-page stacks are much less forgiving for:

- medium/large local arrays,
- deep call chains,
- recursive code,
- feature growth over time.

## What this means as auxv6 grows

Without policy changes, stack-pressure bugs will recur in user utilities as we
add functionality. They often appear as odd data-path behavior, not clean
crashes, and can waste debugging time.

## Recommended plan

### Phase 1 (recommended now): larger fixed stack

Keep current model, but increase stack pages at `exec` time.

Suggested default:

- Guard page: 1 page (unchanged).
- User stack: 4 to 8 pages (16 KiB to 32 KiB).

Implementation sketch:

1. Add constants, for example:
   - `USER_STACK_GUARD_PAGES = 1`
   - `USER_STACK_PAGES = 4` (or 8)
2. Update stack allocation in `kernel/core/exec.c` to allocate
   `USER_STACK_GUARD_PAGES + USER_STACK_PAGES`.
3. Keep guard page as non-user-accessible (`clearpteu` on guard range).
4. Set initial `sp` to top of stack region as today.
5. Mirror policy in early init process setup path (`userinit`) so behavior is
   consistent for all processes.

Pros:

- Low complexity and low risk.
- Immediate reduction in stack-related userland bugs.

Tradeoff:

- Higher per-process memory footprint.

### Phase 2 (optional later): demand-growing stack

Add page-fault-driven stack growth (downward) with strict bounds.

High-level requirements:

1. Track per-process stack bounds (top, current bottom, max depth).
2. In page fault handling, allow growth only when fault address is within a
   valid growth window below current stack bottom.
3. Allocate one (or a few) new stack pages and keep at least one guard page.
4. Kill process on invalid growth attempts.

Pros:

- Better memory efficiency.
- More mainstream behavior.

Tradeoff:

- Significantly higher implementation and testing complexity.

## Engineering guidelines for userland (applies now)

- Avoid multi-KiB local arrays on stack.
- Prefer static buffers or heap allocations for large work buffers.
- Avoid deep recursion in utilities.
- Keep path/temp locals compact.
- Consider compiler warnings for large stack frames during CI.

## Minimal regression ideas

1. Add a small test utility that uses controlled local buffer sizes and checks
   that expected sizes run safely.
2. Add copy-style workload tests (`cp`, `cat > file`, cross-filesystem copies)
   to catch data-path regressions quickly.
