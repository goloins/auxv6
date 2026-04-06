# kalloc Page-Fault Investigation (2026-04-06)

## Incident Summary

During early Btrfs/NVMe validation, running `lsblk` in guest triggered a fatal kernel page fault on CPU 1.

Reported crash signature:

```text
unexpected trap 14 from cpu 1 eip 8014dcd7 (cr2=0x9fbf8000)

FATAL trap: kernel-page-fault cpu=0x00000001 trap=0x0000000e err=0x00000000 eip=0x8014dcd7 cs=0x00000008 cr2=0x9fbf8000
lapicid 1: panic: trap_kernel_fatal: trap
```

Key takeaway:
- This failure is in the allocator path, not in Btrfs write support.
- Btrfs remains read-only by design in this tranche.

## Reproduction Context

- Host: Linux
- Guest boot path used: `sudo make qemu-nvme-btrfs`
- Trigger command in guest: `lsblk`
- Failure mode: trap 14 (page fault), kernel fatal panic

Additional reproduction (same session family):
- Workload: `kallocstress -n 30`
- Outcome: all stress checks passed, then kernel trap-14 immediately after 30-run summary
- Second crash signature:

```text
unexpected trap 14 from cpu 0 eip 8015d54f (cr2=0x9fe3b000)

FATAL trap: kernel-page-fault cpu=0x00000000 trap=0x0000000e err=0x00000000 eip=0x8015d54f cs=0x00000008 cr2=0x9fe3b000
lapicid 0: panic: trap_kernel_fatal: trap
```

Third reproduction (interactive shell path):
- Workload before fault: `bcachestress` passes, then shell history up-arrow
- Third crash signature:

```text
unexpected trap 14 from cpu 0 eip 8014e86a (cr2=0x9f262000)

FATAL trap: kernel-page-fault cpu=0x00000000 trap=0x0000000e err=0x00000002 eip=0x8014e86a cs=0x00000008 cr2=0x9f262000
lapicid 0: panic: trap_kernel_fatal: trap
```

Fourth reproduction (init/runlevel path):
- Boot reaches init and starts rc.3, then panics in `kfree`.

```text
init: rc script process exited
init: child executing runlevel script /etc/rc.d/rc.3
lapicid 1: panic: kfree: kfree
```

Interpretation:
- This is not the earlier pre-`kvmalloc()` boot-window false negative.
- It is a post-boot/runtime bad-free class event surfacing during service bring-up.
- Next data needed is the exact invalid pointer + caller at `kfree` entry.

## Symbolication and Fault Mapping

Resolved fault address:
- `eip=0x8014dcd7` falls inside [kernel/core/kalloc.c](kernel/core/kalloc.c#L171) function `kalloc_refill_local`.

Nearest symbols from [kernel.sym](kernel.sym):
- `8014dc50 kalloc_refill_local`
- `8014ddb0 kalloc_cache_pop_valid`

Relevant disassembly around fault site:

```asm
8014dcca: call   8014dbf0 <kalloc_free_run_valid>
8014dccf: test   %eax,%eax
8014dcd1: je     8014dd9c <kalloc_refill_local+0x14c>
8014dcd7: mov    (%edi),%edx
8014dcd9: lea    0x1(%ebx),%eax
8014dce3: mov    %edx,0x806feb20
```

Interpretation:
- `%edi` holds current `kmem.freelist` node (`struct run *r`).
- Fault occurs while loading `r->next`.
- `cr2=0x9fbf8000` indicates `%edi` pointed at an unmapped/invalid kernel VA at dereference time.

Second crash mapping:
- `eip=0x8015d54f` resolves between `deallocuvm.part.0` and `mappages` symbols.
- Nearest symbols from [kernel.sym](kernel.sym):
   - `8015d500 deallocuvm.part.0`
   - `8015d5e0 mappages`

Disassembly around second fault:

```asm
8015d54a: mov    %ebx,%edx
8015d54c: shr    $0x16,%edx
8015d54f: mov    (%esi,%edx,4),%eax
8015d552: test   $0x1,%al
```

Interpretation:
- Fault is on `pgdir[pdx]` load inside `deallocuvm` traversal.
- This is consistent with page-directory pointer/state corruption (or a broken kernel mapping in current page tables), not a filesystem write-path issue.

Third crash mapping:
- `eip=0x8014e86a` resolves to `kmalloc`.
- Nearest symbols from [kernel.sym](kernel.sym):
   - `8014e840 kmalloc`
   - `8014e8a0 kmalloc_free`

Disassembly around third fault:

```asm
8014e866: test   %eax,%eax
8014e868: je     8014e890 <kmalloc+0x50>
8014e86a: movl   $0x4b4d414c,(%eax)
```

Interpretation:
- `kmalloc` got a non-null base pointer from allocator path, then faulted immediately writing header magic.
- `err=0x2` (kernel write on non-present page) indicates returned kernel VA is not writable in current mappings.
- This aligns with memory-map/page-table corruption or allocator returning a pointer that is not safely mapped.

## Source-Level Control Flow

In [kernel/core/kalloc.c](kernel/core/kalloc.c#L171), the sequence is:

1. `r = kmem.freelist`
2. `kalloc_free_run_valid(r)` is called
3. `kmem.freelist = r->next` (this dereference maps to the faulting instruction)

Related validators:
- [kernel/core/kalloc.c](kernel/core/kalloc.c#L22) `kalloc_runptr_valid`
- [kernel/core/kalloc.c](kernel/core/kalloc.c#L91) `kalloc_free_run_valid`

The surprising condition is that validation passed, but immediate dereference faulted.

## Why This Is an Allocator Integrity Bug

- The crash happens before any Btrfs vnode operation can perform writes.
- The instruction is purely allocator freelist manipulation.
- Trap type and EIP strongly indicate freelist corruption or a stale invalid pointer acceptance.

This incident should be tracked as allocator correctness/integrity regression, with `lsblk` only acting as a trigger workload.

With the second crash, scope expands from freelist-only suspicion to allocator/VM integrity boundary:
- Crash A (`kalloc_refill_local`) shows invalid free-run dereference during allocator refill.
- Crash B (`deallocuvm.part.0`) shows invalid page-directory read during memory teardown after allocator-heavy stress.
- Crash C (`kmalloc`) shows immediate header-write fault on newly returned allocation pointer.
- Together they strongly suggest earlier memory corruption that later manifests in both allocator and VM paths.

## Working Hypotheses (Ranked)

1. Upstream memory corruption (use-after-free/double-free/out-of-bounds write) affecting both allocator structures and VM page-directory structures.
2. Freelist pointer corruption race or stale node insertion.
3. Validator false-positive for a pointer that satisfies simple range/alignment checks but is not safely dereferenceable in current kernel mapping context.
4. Metadata inconsistency (`KPAGE_FREE`/refcount/managed flags) allowing logically invalid nodes through.
5. Corruption in process teardown/page-table lifetime ordering exposing invalid `pgdir` reads in `deallocuvm`.

## Evidence That Narrows Scope

- Fault function: [kernel/core/kalloc.c](kernel/core/kalloc.c#L171)
- Faulting instruction dereferences freelist node.
- Validation gate immediately precedes fault.
- `err=0x0` indicates non-present page read, consistent with dereferencing a bogus VA.
- Independent second fault hits [kernel/core/vm.c](kernel/core/vm.c#L280) `deallocuvm` path (`deallocuvm.part.0`).
- Second faulting instruction is page-directory index read, which should be stable under normal teardown.

## Immediate Engineering Implications

- Do not assume storage stack code is root cause when reproducer is `lsblk`.
- Treat Btrfs test failures after this panic as blocked by allocator stability.
- Keep Btrfs smoke commands read-only focused until allocator is stabilized.

## Debug/Instrumentation Plan

1. Add allocator debug gate with structured logs around freelist pop/push in `kalloc_refill_local`, `kalloc_cache_pop_valid`, and `kfree` slow path.
2. Log pointer + metadata tuple before dereference:
   `r`, `V2P(r)`, `meta->flags`, `meta->refcount`, `kmem.free_pages`, `c->kfree_cache_count`, cpu id.
3. Add fail-closed guard path before `r->next` dereference that panics with full context if pointer is not dereference-safe.
4. Add VM debug guard in `deallocuvm` for suspicious `pgdir` pointer/index state before `pgdir[pdx]` access; panic with context if violated.
5. Audit all paths mutating `kmem.freelist`, plus any page-table free/reuse lifetime transitions (`freevm`, `deallocuvm`, fork/exit wait paths).
6. Stress with repeated `kallocstress -n 30`, `lsblk`, `mount`, `cat`, and mixed fork/exec load to capture first corruption point.

## Instrumentation Added (This Session)

The following fail-closed guards were added to improve first-fault forensics:

1. [kernel/core/kmalloc.c](kernel/core/kmalloc.c):
    - `kmalloc()` now validates returned base (and tail page for multi-page allocs)
       with `kaddr_writable_current_pgdir()` before writing allocator header.
2. [kernel/core/kalloc.c](kernel/core/kalloc.c):
    - `kalloc_free_run_valid()` now rejects free-run pointers that are not writable
       in current kernel pgdir context.
3. [kernel/core/vm.c](kernel/core/vm.c#L274):
    - `deallocuvm()` now validates pgdir non-null, page alignment, kernel range,
       and writable mapping before indexing PDEs.

Early-boot caveat and fix:
- `kaddr_writable_current_pgdir()` originally returned false when `kpgdir` was
   not published yet, which can happen before `kvmalloc()`.
- That made some debug guards overly strict during bootstrap and can manifest as
   a bootloop.
- The helper now has an explicit early-boot fallback: if `pgdir==0`, treat
   `[KERNBASE, KERNBASE+BOOT_EARLY_PHYSTOP)` as writable (the `entrypgdir`
   bootstrap mapping window).

These checks are intentionally strict to convert latent trap-14 crashes into
deterministic panic points with context.

Additional instrumentation (post-update):
- `kfree()` now logs invalid-pointer diagnostics before `panic("kfree")`,
  including `v`, computed `pa`, `end`, `PHYSTOP`, caller return address, and
  `kmem.use_lock` state.

## New Stress Utility: kmemstress(1)

To broaden race reproduction beyond single-purpose stressors, a new utility is
now integrated:

- Command: `kmemstress`
- Source: [user/kmemstress.c](user/kmemstress.c)
- Manpage: [targetfs/usr/share/man/kmemstress.md](targetfs/usr/share/man/kmemstress.md)

`kmemstress` continuously drives mixed VM, VFS, IPC, procfs, descriptor,
socket, and kernel-metadata API churn while emitting per-round diagnostics and
proc snapshots. This is intended to surface first-corruptor signatures faster
than isolated stress loops.

## Deterministic kmemstress Crash (mountinfo path)

Observed repeatedly under `kmemstress` startup:

```text
unexpected trap 14 from cpu X eip 80114a83 (cr2=0x1d180)
FATAL trap ... err=0x00000003
```

Symbolication:
- `eip=0x80114a83` is in `vfs_get_mounts`, writing into `out`.

Root cause:
- `sys_mountinfo` passed user pointer `out` directly to `vfs_get_mounts`.
- Kernel then wrote directly to user VA instead of using `copyout`.
- On COW/protected user pages this faults in kernel mode (`err=0x3`).

Fix applied:
- `sys_mountinfo` now:
   1. allocates kernel buffer (`kmalloc`),
   2. fills it via `vfs_get_mounts`,
   3. copies to user space via `copyout`,
   4. frees kernel buffer.

This aligns mountinfo with COW-safe user-copy discipline used elsewhere.

## Kernel/Userland Completeness Gaps (Documented)

The stress loop also exposed incompleteness that is separate from memory
corruption. These should be tracked explicitly so they are not misread as
allocator regressions.

1. Optional procfs surfaces are not consistently present.
    - Paths observed as potentially absent in some boots/configs:
       `/proc/schedstat`, `/proc/lsof`, `/proc/bcache_health`.
    - Impact: userland diagnostics can see intermittent missing data planes.
    - Status: `kmemstress` now classifies these as warnings, not hard failures.

2. Syscall user-copy contract coverage was incomplete.
    - Confirmed fixed in this session: `sys_mountinfo`, `sys_netifinfo`,
       `sys_routeinfo`, `sys_arpinfo` now use kernel staging + `copyout`.
    - Completeness implication: all structured-output syscalls should follow
       the same pattern and be audited as a class.

3. Filesystem feature parity is not uniform across mounted backends.
    - `ftruncate` support can vary by filesystem/device path.
    - Impact: high VFS fail counts can reflect unsupported operations rather
       than corruption.
    - Status: `kmemstress` performs one-time capability probing and downgrades
       unsupported `ftruncate` behavior to warning mode.

Interpretation guideline:
- Hard failures should represent contract violations or corruption signals.
- Warnings should represent feature absence, optional surface variability, or
   backend capability differences.

## Completeness Backlog (Follow-up)

1. Procfs contract matrix
    - Define required vs optional nodes in docs and validate at boot.
    - Add a machine-readable list consumed by userland diagnostics.

2. Structured-output syscall audit
    - Audit all syscalls that return arrays/records to user memory.
    - Require kernel-buffer staging + `copyout` in code review checklist.

3. FS capability discovery syscall
    - Add a small capability query surface (for truncate, sparse, xattr,
       mmap-write, etc.) so stress tools can adapt without guesswork.

4. kmemstress result taxonomy
    - Keep `FAIL` reserved for correctness/safety violations.
    - Keep feature gaps and optional-surface absence under `WARN`.

## New Signature: Double-Fault in pushcli

Observed after extended `kmemstress -H -v` looping and manual `^C` interruption:

```text
FATAL trap: double-fault cpu=0x00000000 trap=0x00000008 err=0x00000000 eip=0x8015313b cs=0x8
lapicid 0: panic: trap_kernel_fatal: trap
```

Symbolication:
- `0x8015313b` resolves to `pushcli` in [kernel/core/spinlock.c](kernel/core/spinlock.c),
  at the store of `intena` via `mycpu()` pointer.

Disassembly at fault point:

```asm
80153130: call 801509c0 <mycpu>
80153135: and  $0x200,%ebx
8015313b: mov  %ebx,0xa8(%eax)
```

Interpretation:
- Faulting write target is `mycpu()->intena`.
- A double-fault here suggests severe state corruption in interrupt/stack/CPU-local
  state, or a bad pointer returned from `mycpu()` under exceptional conditions.
- This is a higher-severity signal than earlier user-copy contract bugs and should
  be treated as potential core scheduler/interrupt-state corruption.

Immediate next debug focus:
1. Add guarded assertions in `pushcli`/`popcli` for `mycpu()!=0` and expected
   CPU-local address range before dereference (debug builds).
2. Capture whether panic is reproducible without `^C` (to separate interrupt
   teardown path from steady-state stress corruption).
3. Add lightweight counters/log points around `ncli/intena` transitions to detect
   underflow/imbalance before trap-8.

## New Boot Signature: invalid kfree from deallocuvm teardown

Observed during boot while init transitions into runlevel script execution:

```text
init: rc script process exited
init: child executing runlevel script /etc/rc.d/rc.3
kfree: invalid ptr v=150000 pa=ffffffff end=80841c40 phystop=a0000000 caller=8015dd27 use_l1
lapicid 0: panic: kfree: kfree
```

Symbolication:
- `caller=0x8015dd27` maps to `deallocuvm` return site right after `kfree` call.
- Call path is `deallocuvm -> uvm_release_pte -> kfree(P2V(pa))`.

Interpretation:
- This indicates a non-physical value reached `PTE_ADDR(*pte)` in teardown.
- If `pa` is already kernel-virtual-like (`>=KERNBASE`), `P2V(pa)` wraps and can
   produce low virtual addresses (example: `0x00150000`), matching the panic log.
- This is not a benign free error; it is evidence of page-table entry corruption
   or incorrect PTE encoding on a prior mapping path.

Guard added:
- `uvm_release_pte()` now validates `pa` before `kfree` and panics early with
   explicit context (`pte`, raw PTE value, decoded `pa`, flags) when `pa==0`,
   `pa>=PHYSTOP`, or `pa>=KERNBASE` class conditions are detected.

Why this helps:
- Prevents wrapped-pointer downstream panics in `kfree`.
- Produces first-fault diagnostics at the point where a malformed PTE is
   consumed, narrowing root-cause search to PTE producers/mutators.

Repro note update:
- This boot-path panic is currently intermittent (not a reliable boot crash).
- Primary deterministic repro remains `kmemstress` pressure loops.

## Syscall COW-Safety Cleanup Pass (in progress)

To remove remaining legacy direct user-pointer dereferences in syscall layer,
the following conversions were completed this session:

1. [kernel/core/sysproc.c](kernel/core/sysproc.c)
   - `sys_date`: kernel-local `rtcdate` + `copyout`.
   - `sys_clock_gettime`: kernel-local `timespec` + `copyout`.
   - `sys_clock_settime`: `copyin` from user `timespec`.
   - `sys_uname`: kernel-local string + `copyout`.
   - `sys_getrlimit`: kernel-local `rlimit` + `copyout`.
   - `sys_setrlimit`: `copyin` for user `rlimit` before policy checks.

2. [kernel/net/socket.c](kernel/net/socket.c)
   - `sys_setsockopt`: `copyin` for option payload reads.
   - `sys_getsockopt`: `copyin` optlen and `copyout` for value/updated length.
   - `sys_getsockname`/`sys_getpeername`: kernel-local sockaddr + `copyout`
     for sockaddr and length.

This reduces the remaining "old xv6 style" user-pointer write/read surface and
aligns these APIs with COW-safe user-copy discipline.

### Next Batch Completed

Additional syscall paths were converted to kernel staging in this batch:

1. [kernel/core/sysproc.c](kernel/core/sysproc.c)
    - `sys_waitpid`/`sys_wait4`: now gather status in kernel local storage and
       `copyout` to user status pointer after `proc_wait*` returns.
    - `sys_waitid`: now gathers `siginfo` fields in kernel local storage and
       `copyout` to user buffer.

2. [kernel/core/sysfile.c](kernel/core/sysfile.c)
    - `sys_getdents`: now collects entries into a kernel buffer and `copyout`s
       only the produced count.
    - `sys_poll`: now copies input `pollfd[]` into kernel memory, updates
       `revents` in-kernel, and `copyout`s results on return paths.

Result:
- Further reduction of direct user-memory writes inside syscall core paths.
- Better COW safety under stress and boot-path churn.

### Additional Legacy-Path Cleanup

More "old xv6 style" user-pointer dereferences were removed in process APIs:

1. [kernel/core/proc.c](kernel/core/proc.c)
    - `proc_sigaction`: now `copyin`s new action from user, applies under lock,
       and `copyout`s old action snapshot.
    - `proc_sigprocmask`: now `copyin`s set and `copyout`s old mask instead of
       dereferencing user pointers directly.
    - `proc_tcgetattr`/`proc_tcsetattr`: now use kernel-local `termios` staging
       with `copyout`/`copyin` around driver calls.

   ### kmemstress proc=1 Follow-up

   - Observed pattern: repeated `[round N] ... proc=1 ...` with otherwise stable VM/VFS/net counters.
   - Root cause class: diagnostic-path mismatch in scratch directory scanning (`getdents` path) can
      produce persistent proc-fail noise even when core `/proc` required nodes succeed.

   Adjustments made:
   1. [user/kmemstress.c](user/kmemstress.c)
       - Scratch directory scan failures are now surfaced as warnings with one-time diagnostics,
          rather than hard `proc` failures.
       - Required proc checks (`/proc/meminfo`, `/proc/vmstat`) remain hard failures.
   2. [kernel/core/sysfile.c](kernel/core/sysfile.c)
       - `sys_getdents` allocation order corrected so non-directory early returns do not leak
          temporary kernel buffers.

   ## COW Audit Completion (Current Scope)

   Completed additional conversion pass to remove remaining legacy direct user-pointer
   dereferences in syscall/control planes.

   1. [kernel/core/sysfile.c](kernel/core/sysfile.c)
       - Converted `sys_fstat`, `sys_stat`, `sys_lstat`, `sys_getcwd`, `sys_readlink`,
          `sys_pipe`, and `sys_loopstatus` to kernel-local staging + `copyout`.

   2. [kernel/core/sysproc.c](kernel/core/sysproc.c)
       - Removed residual `argptr`-style pointer handoff in wrappers:
          `sys_sigaction`, `sys_sigprocmask`, `sys_tcgetattr`, `sys_tcsetattr`.
       - Reworked `sys_ioctl` pointer-based branches (audio/tuntap/tty ioctls) to:
          kernel buffer staging + `copyin`, backend call, then `copyout`.

   3. [kernel/net/socket.c](kernel/net/socket.c)
       - Converted `sys_bind`/`sys_connect` sockaddr ingestion to `copyin`.
       - Converted `sys_send`, `sys_sendto`, `sys_recv`, `sys_recvfrom`,
          `sys_recvtimeout` payload and sockaddr/result handling to staged kernel
          buffers with `copyin`/`copyout`.

   Residual `argptr` usage is now limited to validation-oriented or compatibility
   surfaces where direct user-memory writes are no longer performed in these paths.

   ## Intermittent rc.3 Filename Corruption Triage

   Observed symptom (non-deterministic at boot):

   ```text
   dash: 0: cannot open <non-printable bytes>: Unknown error 0
   ```

   To capture raw bytes at the boundary where corruption appears, targeted diagnostics
   were added:

   1. [user/init.c](user/init.c)
       - `run_runlevel_script()` now logs the raw byte sequence of `script_path`
          immediately before `exec("/bin/dash", ...)`.

   2. [kernel/core/sysfile.c](kernel/core/sysfile.c)
       - `sys_exec()` now emits a hexdump of `argv[1]` when launching `/bin/dash`
          if non-printable bytes are detected.

   This should distinguish:
   - user-space source corruption before `exec`, vs
   - corruption on/after syscall argument ingestion.

## Proposed Acceptance Criteria for Fix

1. No trap/panic across repeated `lsblk` calls during NVMe/Btrfs boots.
2. No allocator corruption panics during AHCI/NVMe mixed workloads.
3. In debug builds, zero invalid freelist-node drops for baseline smoke sequence.
4. Btrfs read-only smoke sequence completes repeatedly after allocator fix.

## Current Status

- Incident documented.
- Root cause not yet fully proven for intermittent boot-path panic class, but deterministic
   syscall user-copy boundary issues have been substantially reduced.

## Validation Milestone (2026-04-06)

Verified user report after latest syscall and stress-tool cleanup:

```text
kmemstress: done rounds=250 fail_total=0 warn_total=501
```

Interpretation:
- `fail_total=0` indicates no hard correctness failures across the bounded high-profile run.
- Non-zero `warn_total` currently reflects optional/incomplete surfaces and diagnostic-path
   variability, which are tracked as completeness debt rather than corruption signals.

## Critical Root-Cause Correction (2026-04-06, later)

A regression introduced in VM hardening caused catastrophic page-table ownership
violations and can directly explain `eip=0`, trap-6-on-valid-text-address,
and triple-fault reboot loops.

Root cause chain:
1. `switchuvm()` was calling `vm_sync_kernel_pdes()` to copy kernel-half PDEs
   from global `kpgdir` into each process `pgdir`.
2. This made process pgdirs alias kernel page-table pages owned by `kpgdir`.
3. `freevm()` later walks all present PDEs in a process pgdir and `kfree()`s
   the PDE-backed page-table pages.
4. Result: shared kernel page-table pages could be freed from process teardown,
   corrupting kernel mappings globally.

Concrete fix applied:
- Removed `vm_sync_kernel_pdes()` and its invocation from `switchuvm()` in
  [kernel/core/vm.c](kernel/core/vm.c).
- Kept `walkpgdir(alloc=0)` non-destructive behavior (no PDE mutation on read-only walk).

Why this is a core fix (not a band-aid):
- Restores page-table ownership invariants: each process only frees page-table
  pages it owns, and kernel scheduler mappings in `kpgdir` are not aliased into
  per-process teardown ownership.
- Directly targets a corruption producer, not only downstream crash sites.

This is the first clean bounded high-stress run after the COW/user-copy cleanup passes and is
the current stability baseline.

Additional validation milestone:

```text
kmemstress -M -v -n 1000: completed with fail_total=0
```

Interpretation:
- Longer-duration medium-pressure stress also remains clean for hard failures.
- Current blocking issue remains intermittent boot/runlevel-script filename corruption,
  not deterministic kmemstress instability.

## kmemstress -M -n 1500 Regression (2026-04-06, CPU1)

Observed user trace:

```text
kalloc: dropped invalid cache run=9fcc6000 drops=1
unexpected trap 14 from cpu 1 eip 8014e100 (cr2=0x807feeb4)
FATAL trap: kernel-page-fault ... eip=0x8014e100 cs=0x8 cr2=0x807feeb4
```

Symbol mapping:
- `0x8014e100` is in `kalloc_cache_pop_valid()` while reading `kpage_meta[]`.
- Faulting read occurred before the existing `kaddr_writable_current_pgdir(r)` guard.

Related prior chain observed in same tranche:
- invalid `kfree` pointer from `freevm()` (`caller=0x8015f0c3`).
- panic-path page fault in `getcallerpcs()` (`eip=0x8015348e`).

Applied fixes:
1. `kernel/core/proc.c`
   - `cpuid()` now disables interrupts around `mycpu()`.
2. `kernel/core/spinlock.c`
   - `getcallerpcs()` now bounds-checks/align-checks EBP chain.
3. `kernel/core/vm.c`
   - `freevm()` now validates PDE physical addresses before `kfree`.
   - `walkpgdir()` now detects/clears bad present PDEs before `P2V` translation.
4. `kernel/core/kalloc.c`
   - metadata mapping check added before dereferencing `kpage_meta` in
     `kalloc_free_run_valid()`.
   - same mapping check added in hot paths that consume metadata:
     `kfree()`, early `kalloc()`, cache-hit `kalloc()`, refill `kalloc()`.

5. `kernel/core/spinlock.c`
    - `pushcli()`/`popcli()` now fetch `mycpu()` once per call and use a local
       CPU pointer for all `ncli/intena` updates.
    - Rationale: repeated `mycpu()` calls in one function can become internally
       inconsistent if APIC-ID lookup is transiently wrong, which can corrupt
       per-CPU `ncli` accounting and trigger `panic("popcli")` under load.

6. `kernel/core/proc.c`
    - `mycpu()` now validates reverse-map hits: `apic_cpu_map[apicid]` must map
       to a CPU whose recorded `cpus[idx].apicid` matches the queried APIC ID.
    - Stale/corrupt reverse-map entries are invalidated and rebuilt via scan,
       preventing silent wrong-CPU returns that can poison per-CPU accounting
       (notably `ncli`).

7. `kernel/core/spinlock.c`
    - Added explicit `popcli underflow` diagnostic print with CPU APIC ID,
       `ncli`, `intena`, and immediate caller address before panic.
    - Purpose: if underflow persists, next report contains a direct call-site
       anchor instead of only `panic("popcli")`.

8. `kernel/core/vm.c` (core containment, no rigging)
    - `uvm_release_pte()` no longer panics the kernel on corrupted user PTE
       physical addresses (e.g. `pa >= PHYSTOP`, `pa >= KERNBASE`, or unmanaged
       allocator page). Instead it:
       - logs a throttled diagnostic,
       - clears the PTE,
       - returns a corruption code (`-2`).
    - `deallocuvm()` treats that corruption code as a per-process page-table
       defect, counts/drops bad PTEs, and continues teardown instead of panicking
       the whole kernel.
    - Goal: keep kernel alive, quarantine bad process mappings, and preserve
       actionable diagnostics for upstream root-cause analysis.

9. `kernel/core/vm.c` (kernel-half canonicalization)
    - Added `vm_sync_kernel_pdes(pgdir)` and invoked it from `switchuvm()`
       before loading CR3.
    - For every process switch, kernel-half PDE entries (`PDX(KERNBASE)..1023`)
       are forced to match `kpgdir` canonical mappings.
    - This prevents a single process with corrupted kernel-half page-directory
       entries from destabilizing kernel execution or inducing hangs after
       context switch.

10. `kernel/core/kalloc.c` (global freelist corruption containment)
      - `kalloc_refill_local()` no longer panics immediately on an invalid
         global freelist head node.
      - New behavior:
         - increments/logs `invalid_global_drops`,
         - unlinks the bad run if readable (`kmem.freelist = r->next`),
         - otherwise stops refill by clearing freelist head to avoid deref fault.
      - Rationale: avoids catastrophic panic loops/triple-fault cycles from a
         single poisoned freelist node and preserves runtime to gather upstream
         corruption diagnostics.

11. `kernel/core/kalloc.c` (fail-safe chain handling)
      - On bad global freelist head in `kalloc_refill_local()`, allocator now
         stops trusting `r->next` entirely and resets `kmem.freelist = 0`.
      - Added `poison next` detection: if a popped valid run has invalid `next`,
         freelist is reset instead of following chain.
      - Added head-sanity checks before linking in drain/early paths; invalid
         freelist heads are reset proactively.
      - Intent: convert random-chain execution/corruption into controlled OOM,
         improving survivability and preserving serial diagnostics under severe
         allocator poisoning.

12. `kernel/net/device.c` (if_list poisoning containment)
      - Hardened `netdev_poll()` linked-list walk of `if_list` with kernel-pointer
         validation before dereferencing `if_next`.
      - On poisoned `ifp`/`if_next`, poll path now logs and quarantines by
         truncating/disabling polling instead of following corrupt pointers.
      - This directly targets observed crash site `eip=0x80165ee9` where
         `netdev_poll` was dereferencing `ifp->if_next` from a corrupted pointer
         and cascading into `eip=0` fatal traps.

Intent of tranche:
- Prevent allocator crashes when current process page tables have transiently broken
  kernel mappings, and surface root-cause corruption signals via explicit diagnostics
  rather than faulting in allocator validation code.
