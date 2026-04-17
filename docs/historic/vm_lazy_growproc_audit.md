# Lazy `growproc()` Compatibility Audit

Date: 2026-04-12

This audit answers one narrow question:

What kernel paths currently assume that a successful process size increase implies user pages are already mapped, and therefore would break or change behavior if `growproc()` were converted from eager allocation to lazy reservation?

The answer is: a lot of syscall-visible behavior still depends on eager mappings. The current explicit `vmreserve()` path is safe because it is opt-in and fault-only. A lazy `growproc()` conversion would be a much larger semantic change.

## Current Baseline

Today the system has two distinct models:

- normal heap growth is eager via `growproc()` and `allocuvm_as()`
- explicit anonymous reservation is lazy via `vmreserve()` plus fault-time `VMA_ZEROFILL` materialization

That split is intentional and currently correct.

## Audit Result

### Safe Or Mostly Safe Under Reservation Semantics

These paths already behave in a way that does not inherently require every reserved page to be mapped immediately.

- fork VMA inheritance:
  - `address_space_dup_cow()` copies VMA metadata directly
  - `copyuvm_internal()` skips absent PTEs instead of panicking on them
  - result: untouched reserved pages survive fork as reservations rather than materialized mappings
- user fault dispatch:
  - the page-fault layer already has an isolated demand-zero path for `VMA_ZEROFILL`
- address-space guard policy:
  - `vm_addrspace_allows_user_va()` uses VMA coverage, so reserved-but-legal virtual addresses are already recognized as valid address-space members

### Hard Blockers For Lazy `growproc()`

These are the places where the kernel touches user memory and currently expects the page to already exist.

#### 1. `copyout()` still requires a present writable page

Code:

- [kernel/core/vm.c](kernel/core/vm.c#L1198)

Current behavior:

- validates VMA membership
- walks the PTE
- fails immediately if the PTE is absent
- only special-cases COW, not absent pages

Why this blocks lazy heap growth:

- a newly `sbrk()`-grown buffer handed to a syscall like `read()` would still be legally within the process size
- but if the page had not yet been touched by user mode, `copyout()` would fail instead of creating it

Impact surface:

- `read()` family writes data into user buffers
- wait/status syscalls write result structures to user memory
- many filesystem and procfs syscalls write structures or strings back to user buffers
- signal delivery writes a signal frame onto the user stack
- exec writes argv strings and pointer tables onto the new user stack

This is the single biggest compatibility blocker.

#### 2. `copyin()` still requires a present readable page

Code:

- [kernel/core/vm.c](kernel/core/vm.c#L1254)
- [kernel/core/syscall.c](kernel/core/syscall.c#L18)
- [kernel/core/syscall.c](kernel/core/syscall.c#L49)
- [kernel/core/sysfile.c](kernel/core/sysfile.c#L47)

Current behavior:

- checks VMA membership
- converts the user VA to a kernel VA with `uva2ka()`
- fails immediately if the PTE is absent

Why this blocks lazy heap growth:

- syscall argument fetching, pathname copying, `ioctl` argument copying, `select`/`poll` masks, and many read-from-user paths all assume the user page is already instantiated
- if a process allocated memory and passed it to a syscall without first touching it in user mode, `copyin()` would fail even though the address would be within the logical heap

Semantically, that is not acceptable for transparent lazy heap growth.

#### 3. `argptr()` only bounds-checks by `sz`

Code:

- [kernel/core/syscall.c](kernel/core/syscall.c#L91)

Current behavior:

- verifies pointer range against `curproc->sz`
- does not verify present mappings

Why this matters:

- under eager heap growth, this is fine because later `copyin()`/`copyout()` will succeed if the pointer is in range
- under lazy heap growth, the bounds check would continue to pass, but later accesses could fail on absent pages

This is not the root problem by itself, but it is part of the semantic mismatch between "valid address range" and "materialized user memory."

### Exec And Signal Paths That Depend On Eager Mappings

#### 4. `exec()` builds the new user stack with `copyout()`

Code:

- [kernel/core/exec.c](kernel/core/exec.c#L233)
- [kernel/core/exec.c](kernel/core/exec.c#L244)

Current behavior:

- eagerly allocates program segments with `allocuvm_as()`
- eagerly allocates guard plus stack pages
- writes argv strings and the initial pointer frame with `copyout()`

Why this matters:

- current exec is safe because it eagerly allocates the relevant stack pages first
- if a future design tried to make exec stack setup lazy without additional kernel-side materialization rules, it would break immediately

Conclusion:

- exec is not a blocker for the current explicit reservation model
- exec becomes a blocker only if broader laziness is extended to stack/image construction semantics

#### 5. Signal delivery writes a frame onto the current user stack

Code:

- [kernel/core/proc.c](kernel/core/proc.c#L274)
- [kernel/core/proc.c](kernel/core/proc.c#L363)
- [kernel/core/sysproc.c](kernel/core/sysproc.c#L224)

Current behavior:

- constructs `struct sigframe` in kernel memory
- computes a user stack destination
- uses `copyout()` to place the frame on the user stack
- `sigreturn` later restores the frame using `copyin()`

Why this matters:

- if the destination user stack page were reserved-but-absent, signal delivery would fail before the user ever got control
- current stack growth is only handled on user page faults, not on kernel-to-user copies

Conclusion:

- any future lazy stack model would need an explicit policy for kernel-written stack frames

### Representative Syscall Families That Would Change Behavior

The following families depend on `copyin()` or `copyout()` and therefore on present PTEs today.

#### Kernel writes into user memory

Representative sites:

- [kernel/core/sysproc.c](kernel/core/sysproc.c#L105)
- [kernel/core/sysproc.c](kernel/core/sysproc.c#L167)
- [kernel/core/sysfile.c](kernel/core/sysfile.c#L932)
- [kernel/core/sysfile.c](kernel/core/sysfile.c#L1012)
- [kernel/core/sysfile.c](kernel/core/sysfile.c#L3154)
- [kernel/core/pipe.c](kernel/core/pipe.c#L275)

Examples:

- `wait*()` status buffers
- `read()` into user buffers
- `getdents()` results
- `readlink()` / `getcwd()` string output
- `pipe` reads into user memory

All of these would reject untouched lazy heap buffers today.

#### Kernel reads from user memory

Representative sites:

- [kernel/core/syscall.c](kernel/core/syscall.c#L18)
- [kernel/core/syscall.c](kernel/core/syscall.c#L49)
- [kernel/core/sysfile.c](kernel/core/sysfile.c#L47)
- [kernel/core/sysfile.c](kernel/core/sysfile.c#L1067)
- [kernel/core/sysproc.c](kernel/core/sysproc.c#L675)
- [kernel/core/pipe.c](kernel/core/pipe.c#L207)

Examples:

- raw syscall integer fetching from user stack
- copying pathnames and argument strings
- `write()` from user buffers
- `ioctl` structured inputs
- `termios`, `winsize`, `rlimit`, `select`, and `poll` inputs

These also reject untouched lazy heap or stack pages today.

## Conclusion

Converting `growproc()` to lazy reservation today would not be a small VM-only change. It would alter observable syscall behavior across a large fraction of the kernel/user ABI.

That is why the current explicit `vmreserve()` design is the right checkpoint.

## Recommended Next Safe Slice

Do not change `growproc()` yet.

The next safe engineering step is a policy decision and targeted prototype for kernel-mediated access to reserved anonymous pages.

There are only three coherent options:

1. Keep lazy behavior explicit-only.
   - `vmreserve()` remains the only reservation API
   - user mode must first-touch pages before passing them to syscalls
   - smallest surface area, lowest risk

2. Teach only `copyout()` to materialize `VMA_ZEROFILL` pages.
   - helps `read()`-style destinations and signal/exec stack writes
   - still leaves `copyin()` semantics unresolved
   - narrower than full transparent lazy heap support

3. Teach both `copyin()` and `copyout()` to materialize `VMA_ZEROFILL` pages.
   - this is the full transparent-lazy direction
   - it is exactly the kind of semantic widening that previously caused regressions
   - only safe after explicit policy decisions and focused tests

My recommendation is option 1 or, if you want one more incremental experiment, option 2 under strict `VMA_ZEROFILL` gating only. Option 3 is effectively the start of real lazy-heap semantics and should not be treated as a small change.

## Follow-On Status

Update 2026-04-12:

- option 2 is now prototyped in narrowed form
- `copyout()` may materialize an absent page only when the destination lies in an explicit `VMA_ZEROFILL` reservation
- `copyin()` remains unchanged and still requires present source pages
- `growproc()` semantics remain unchanged
- guest validation passed for the updated `vmreservetest` coverage, including the syscall-output case

That keeps the kernel/user ABI split clear:

- explicit reservations can now be used as syscall output buffers without requiring prior user-mode first touch
- transparent lazy heap semantics are still intentionally deferred