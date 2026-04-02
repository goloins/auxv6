# auxv6 User Stack Sizing and Growth Notes

Date: 2026-04-02

## Why this note exists

We hit a real userland bug where `cp` produced empty output because a large
stack allocation in `cp.c` exhausted the process stack budget.

This note records the current behavior, how it differs from mainstream Unix,
and what to implement next so we do not keep rediscovering the same issue.

## Current auxv6 behavior

Current `exec` stack policy is:

- Allocate `USER_STACK_GUARD_PAGES + USER_STACK_PAGES` after program image alignment.
- Mark guard pages inaccessible.
- Use the remaining pages as the initial user stack.

In code:

- `kernel/core/exec.c`: stack-region allocation loop using
   `USER_STACK_GUARD_PAGES` and `USER_STACK_PAGES`.
- `include/param.h`:
   - `USER_STACK_GUARD_PAGES = 1`
   - `USER_STACK_PAGES = 4`
- `include/mmu.h`: `PGSIZE == 4096`.

So current default user stack budget is 4 pages (16 KiB), minus argv/setup overhead.

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

### Phase 1 (implemented): larger fixed stack

Keep current model, but increase stack pages at `exec` time.

Implemented default:

- Guard page: 1 page.
- User stack: 4 pages (16 KiB).

Implementation notes:

1. Constants live in `include/param.h` and can be tuned without changing `exec.c`.
2. `kernel/core/exec.c` now allocates `(guard + stack)` pages and guards the low region.
3. Initial `sp` remains at the top of the allocated stack region.

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
