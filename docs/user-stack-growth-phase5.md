# User Stack Growth on Demand — Phase 5 Implementation Plan

## Overview

**Status**: Not started (prepared next tranche after devman Phase-2)  
**Goal**: Enable processes to grow their user-space stacks on demand up to a configured maximum, eliminating fixed-size stack overrun crashes.

## Current State

- User stacks are allocated at process creation with a fixed size (`USER_STACK_PAGES`)
- Stack overflow causes immediate SIGSEGV with no recovery path
- Many compute/recursion-heavy workloads hit this limit unnecessarily

## Design

### Data Structures

**`include/proc.h` — add to `struct proc`:**
```c
uint      stack_bot;    /* Virtual address of stack bottom (lowest valid addr) */
uint      stack_guard;  /* Virtual address of guard page (just below bot) */
```

### Constants

**`include/param.h`:**
```c
#define USER_STACK_MAX_PAGES 64  /* Max pages a user stack can grow to */
```

### Core Implementation

1. **Stack allocation in `kernel/core/exec.c`:**
   - After `allocuvm()` creates the initial stack, set `p->stack_bot` and `p->stack_guard`
   - `stack_bot = USTACKBASE - (USER_STACK_PAGES * PGSIZE)`
   - `stack_guard = stack_bot - PGSIZE`

2. **Growth handler in `kernel/core/proc.c`:**
   - Add `proc_try_grow_stack(struct proc *p, uint fault_addr)` function
   - Check if fault is in the guard page or between bot and guard
   - If within limit (total < USER_STACK_MAX_PAGES PGSIZE), call `allocuvm()` to grow
   - Update `stack_bot` downward
   - Return 1 on success, 0 on failure

3. **Page fault handling in `kernel/core/trap.c`:**
   - In `T_PGFLT` case (before SIGSEGV delivery):
   - Check if fault address is near current stack
   - Call `proc_try_grow_stack(p, fault_addr)`
   - Only deliver SIGSEGV if growth attempt fails

4. **Propagation in `kernel/core/proc.c` (fork path):**
   - Copy `stack_bot` and `stack_guard` to child in `fork()`
   - Child inherits parent's stack bottom marker

### Testing Strategy

- Create a deep recursion test that would overflow a 4-page stack but succeed with growth enabled
- Verify SIGSEGV is delivered only when exceeding USER_STACK_MAX_PAGES limit
- Confirm stack grows incrementally (observe via `/proc/ps` SZ field)

## Implementation Sequence

1. Add constants and fields (non-invasive)
2. Update exec.c stack init (single site)
3. Implement proc_try_grow_stack() (isolated function)
4. Wire into trap.c page fault path (single site)
5. Update fork() propagation (single site)
6. Build + test with recursion harness
7. Update docs/user-stack-sizing-and-growth.md with implementation notes

## Known Dependencies

- Requires `allocuvm()` to be callable from proc context (already true)
- Requires trap.c page fault path to have clear error return semantics (already true)
- No new syscalls needed

## Estimated Effort

~4-5 hours of implementation + testing

## Next Session Checklist

- [ ] Read current exec.c and proc.c stack initialization code
- [ ] Design stack_bot/stack_guard initialization formula
- [ ] Implement proc_try_grow_stack() with full bounds checking
- [ ] Wire into trap.c page fault handler
- [ ] Create and run recursion stress test
- [ ] Verify no regressions in normal process creation
