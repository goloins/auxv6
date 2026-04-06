# Audio + Buffer Cache Corruption Investigation (2026-04-05)

## Scope

This note captures a kernel page fault discovered while testing audio telemetry reads from procfs.

## Trigger

In guest shell:

```sh
cat /proc/audio_stats > /tmp/audio_stats.before
```

The system panicked during this command.

## Crash Signature

Observed panic:

```text
unexpected trap 14 from cpu 0 eip 80100aab (cr2=0x8027b848)
FATAL trap: kernel-page-fault cpu=0x00000000 trap=0x0000000e err=0x00000000 eip=0x80100aab
```

## Address Mapping (from built symbols)

- `eip=0x80100aab` maps inside `bread()` in `kernel/fs/bio.c`.
- `cr2=0x8027b848` lies in/near `bcache` memory.
- `bcache` symbol base is `0x80272540` in `kernel.sym`.

Conclusion: fault happened while dereferencing a buffer-cache pointer, not in AC97 I/O register access directly.

## Why This Matters

The failing command reads procfs, but output redirection to `/tmp/...` can involve ext2 block writes and `bread()` activity. If a corrupted hash-chain pointer exists in bcache, unrelated operations can crash on first traversal.

## Evidence from Audio Stats Before Panic Era

Before this panic, AC97 telemetry showed:

- `ac97_present=1`
- large `write_calls` and `bytes_written`
- `ac97_irq_count=0`
- `ac97_bcis_count=0`
- `ac97_lvbci_count=0`
- `ac97_sr=15` (`DCH|CELV|LVBCI|BCIS` set)

This indicated AC97 completion status bits were set in hardware state but ISR was not running.

## Defensive Kernel Change Added

File changed: `kernel/fs/bio.c`

Added hash-chain pointer validation in `bget()`:

1. Validate each hash node pointer points into `bcache.buf[0..NBUF-1]`.
2. If invalid, log explicit corruption line and reset the affected hash bucket.
3. Added equivalent guard while unlinking old hash chains during eviction.

This turns a blind page fault into diagnosable corruption reports.

## Audio Driver and Telemetry Changes Already Present

- `kernel/driver/audio_intel_ac97.c`
  - AC97 debug snapshot exports IRQ and DMA register state.
  - Interrupt enable path updated to set PCI interrupts, AC97 global interrupt gate, and channel interrupt bits.
- `kernel/audio/audio_core.c`
  - `/proc/audio_stats` now includes AC97 telemetry fields.

## Next Factual Step

Re-run the exact trigger and capture any new bcache corruption logs:

```sh
cat /proc/audio_stats > /tmp/audio_stats.before
```

If corruption is present, expected log lines:

- `bget: corrupt hash[...] ...`
- or `bget: corrupt chain hash[...] ...`

Those lines are required to identify who poisoned bcache.

## Status

- Build status after hardening: clean (`aux.kern` rebuilt successfully).
- Root cause of hash corruption: not yet identified.

---

## Second Crash (same session, different boot — 2026-04-05)

### Context

Different boot from the first crash. No audiotone had run yet (`write_calls 0`). `/proc/audio_stats` was read once:

```text
ioctl_calls 1
write_calls 0
bytes_written 0
xruns 0
...
ac97_present 1
ac97_irq_count 0
ac97_poll_count 0
ac97_bcis_count 0
ac97_lvbci_count 0
ac97_fifoe_count 0
ac97_civ 31  ac97_lvi 31  ac97_picb 0
ac97_cr 29   ac97_sr 15
```

AC97 hardware was idle (DMA not running, no writes). The crash occurred during the read
of `/proc/audio_stats` itself.

### Panic

```text
unexpected trap 14 from cpu 0 eip 80100abf (cr2=0x8027b5e0)
FATAL trap: kernel-page-fault cpu=0x00000000 trap=0x0000000e err=0x00000000 eip=0x80100abf
lapicid 0: panic: trap_kernel_fatal: trap
 80169044 5356e589 0 0 0 0 0 0 0 0
```

### Address Mapping

From kernel.asm and binit disassembly:

- `bcache.buf` starts at `0x8027257c`
- `sizeof(struct buf) = 616 = 0x268` (confirmed from `lea 0x268(%ebx),%edx` in `binit` loop)
- `ebx = cr2 - 4 = 0x8027b5dc`
- `(0x8027b5dc - 0x8027257c) / 0x268 = 0x9060 / 0x268 = 60` exactly

**`ebx = &bcache.buf[60]`** — a valid, initialized struct buf.

The faulting address `cr2 = 0x8027b5e0 = ebx + 4` is the `dev` field of `bcache.buf[60]`.

### Why the Guard Did Not Fire

The `bcache_bufptr_valid()` guard added previously checks three conditions (all compiled
inline into `bread()` at `0x80100a9f..0x80100abd`):

1. `b >= bcache.buf` → `0x8027b5dc >= 0x8027257c` ✓
2. `b < bcache.buf + NBUF` → `0x8027b5dc < 0x8028597c` ✓
3. `off < NBUF` → `0x9060 < 0x13198` (36960 < 78232) ✓

All three pass. `bcache.buf[60]` is a fully initialized buffer (buf #60 of 127, well
within bounds). The guard reaches the `b->dev` dereference at `0x80100abf` — and
the read itself faults.

### What err=0x00 Means

- Bit 0 = 0: **page not present** (not a protection violation)
- Bit 1 = 0: read access
- Bit 2 = 0: supervisor mode

Page `0x8027b000` (containing `bcache.buf[58..60]`) is **not mapped** in the kernel
page table at crash time, despite being a static BSS-allocated struct that should always
be present.

### Distinction from First Crash

| | First crash | Second crash |
|---|---|---|
| eip | `0x80100aab` | `0x80100abf` |
| cr2 | `0x8027b848` | `0x8027b5e0` |
| Guard stage | Inside first bounds cmp | After all bounds checks passed |
| buf index | Unknown (guard not yet added) | `bcache.buf[60]` confirmed valid |
| Trigger | `cat /proc > /tmp/file` | `cat /proc/audio_stats` (no redirect) |
| audio DMA active | No | No |
| Page containing cr2 | `0x8027b000` | `0x8027b000` |

Both crashes fault on the **same page** (`0x8027b000`). A single page going non-present
would explain both events.

### Hypothesis: Page Table Corruption

For a statically allocated BSS page to appear non-present, something must have modified
the kernel page table entry for `0x8027b000 → PA 0x0027b000`. Candidates:

1. **AC97 DMA writing to wrong physical address** — if the bounce buffer physical address
   derivation is incorrect (e.g., VA used without V2P conversion), DMA could overwrite a
   kernel PTE page. The BDL entries store 32-bit physical addresses; if a VA is passed
   instead, the DMA writes to an unintended physical region that might include PTE pages.

2. **AC97 setup routine writing to wrong register offset** — a wrong NABM port calculation
   during `ac97_pcmout_setup()` could corrupt kernel memory via I/O port aliasing (less
   likely but possible with emulated hardware).

3. **Unrelated memory corruption** — high-BSS stack overflow or another static array
   overrun, coincidentally hitting page `0x8027b000`.

The intermittency (second boot with same kernel image produced 225 polls and continuous
tone without any crash) is consistent with a DMA-triggered corruption that only fires
if `ac97_pcmout_setup()` runs during a timing window before the page table for
`0x8027b000` has been walked.

### Next Diagnostic Steps

1. **Verify BDL physical addresses** — in `audio_intel_ac97.c`, confirm that all
   `bdl[i].phys` entries are computed as `V2P(bounce[i])`, not as `(uint32_t)bounce[i]`.
   If the VA (with 0x80000000 bit set) is stored directly in the BDL, DMA writes would
   go to physical addresses around 2GB which wrap or corrupt PTE pages.

2. **Add page-presence guard** — before the `b->dev` access in `bget()`, call
   `walkpgdir(kpgdir, (void*)b, 0)` and verify the returned PTE has `PTE_P` set.
   This would produce a diagnostic log entry naming the exact faulting VA and PTE state
   before the crash.

3. **Audit `ac97_pcmout_setup()` BDL address computation** — grep for `V2P` usage
   around bounce buffer assignment; ensure the formula is `(uint32_t)(bounce[i] - KERNBASE)`
   or equivalent, not a bare cast of the pointer.

## Tone Playback Confirmed (2026-04-05)

On a crash-free boot with the same 32-slot BDL kernel, `audiotone` produced a
continuous 5-second tone at 440 Hz:

```text
audiotone: wrote 240000 frames (5000 ms @ 48000 Hz, 2 ch, 440 Hz square) to /dev/pcmC0D0p
```

Post-run stats confirmed DMA was active and advancing:

```text
write_calls 1875
bytes_written 1418752
ac97_irq_count 0
ac97_poll_count 225
ac97_bcis_count 225
ac97_lvbci_count 225
ac97_fifoe_count 0
ac97_civ 0  ac97_lvi 0  ac97_picb 0
ac97_cr 29  ac97_sr 15
```

- 225 polls, 225 BCIS completions → 32-slot BDL cycling continuously as expected.
- `ac97_irq_count` still 0 (polling fallback is carrying all completions).
- `ac97_civ 0 / ac97_lvi 0` after 5 s confirms the DMA completed and wrapped.
- No xruns. Continuous tone, not blips.

## Broader Repro: Non-audio Global Stability Failure (bcachestress)

Repro command (guest):

```sh
bcachestress -w 8 -r 200 -f 16 -k 32
```

This crashes almost immediately and repeatedly, proving the issue is not
audio-path specific.

Observed panic #1 (during stress):

```text
unexpected trap 14 from cpu 0 eip 80144f80 (cr2=0x12540)
FATAL trap: kernel-page-fault cpu=0x00000000 trap=0x0000000e err=0x00000003 eip=0x80144f80
```

Symbol map:

- `0x80144f80` is `memmove+0x3c` (`memmove` base `0x80144f44`)
- faulting VA is low (`cr2=0x00012540`)
- `err=0x3` = protection violation on write in supervisor mode

Observed panic #2 (also seen before login):

```text
unexpected trap 14 from cpu 0 eip 8013fbd2 (cr2=0x9fffc000)
FATAL trap: kernel-page-fault cpu=0x00000000 trap=0x0000000e err=0x00000000 eip=0x8013fbd2
```

Symbol map:

- `0x8013fbd2` is `kalloc_refill_local+0x92` (`kalloc_refill_local` base `0x8013fb40`)
- instruction is `mov (%edx), %edx` while popping `kmem.freelist`
- faulting VA `0x9fffc000` indicates `kmem.freelist` pointed to an unmapped
   kernel virtual page

Interpretation:

- This is consistent with **allocator freelist corruption** and explains why
   unrelated subsystems (including `bread()`) can later crash.
- The `bread()` failures are likely downstream symptoms after allocator metadata
   corruption, not necessarily the first write that poisons memory.

Mitigation added in current tree:

- `kernel/core/kalloc.c` now validates run pointers on freelist pop/push paths
   (`kalloc_refill_local`, `kalloc_drain_local`, `kalloc`, `kfree`) and panics
   with explicit `kalloc freelist corruption` diagnostics naming the failing
   path and bad pointer.

Next run objective:

Re-run the same `bcachestress` command and capture the new explicit panic line
from `kalloc_panic_bad_run(...)`; that should identify first-detected freelist
damage location instead of later secondary page faults.

## Follow-up Repro and Root Cause Confirmation (2026-04-05)

New repro panic:

```text
bcachestress: workers=8 rounds=200 files=16 file_kb=32 dir=/tmp/bcs
unexpected trap 14 from cpu 0 eip 801450ac (cr2=0x12540)
FATAL trap: kernel-page-fault cpu=0 trap=0x0e err=0x00000003 eip=0x801450ac
```

Symbol mapping:

- `0x801450ac` is inside `memmove` (`memmove+0x3c`)
- `cr2=0x12540` maps exactly to `rbuf.2` in `user/bcachestress.sym`

Interpretation:

- The faulting destination is the child user read buffer.
- `err=0x3` means write fault on a present read-only page.
- After `fork`, user pages may be COW-marked read-only.
- Kernel file paths were still performing direct `memmove` into user VAs
  (`readi`, `procfs_copy_data`, mount-root synthetic dirent path), which
  bypasses COW resolution and faults in kernel mode.

This explains immediate crashes under fork-heavy read workloads and is broader
than audio.

Fix applied:

1. `kernel/core/vm.c::copyout` now resolves COW pages via `cow_fault()` before
   writing and rejects non-writable/non-COW user pages instead of blindly
   writing.
2. `kernel/fs/fs.c::readi` now uses `copyout` when destination VA is user
   (`< KERNBASE`), else `memmove` for kernel buffers.
3. `kernel/fs/fs.c::writei` now uses `copyin` when source VA is user, else
   `memmove` for kernel buffers.
4. `kernel/fs/procfs.c::procfs_copy_data` now uses `copyout` for user
   destinations.
5. `kernel/fs/file.c` synthetic `.`/`..` dirent emission now uses `copyout`
   for user buffers.

Expected result:

- `bcachestress -w 8 -r 200 -f 16 -k 32` should no longer fail immediately
  with memmove fault at `cr2=0x12540`.

## Additional Follow-up: tmpfs Backend User-Copy Gap (2026-04-05)

After the first COW-safe copyout/copyin fixes, the same repro still failed with:

```text
unexpected trap 14 from cpu 0 eip 801450ec (cr2=0x12540)
```

`cr2=0x12540` again matched `bcachestress` user read buffer (`rbuf.2`), so a
remaining direct kernel write-to-user path still existed.

Root cause:

- `bcachestress` uses `/tmp/bcs` by default.
- On this system, `/tmp` may be served by tmpfs.
- `kernel/fs/vfs_tmpfs.c` still had raw `memmove`/`memset` user-buffer writes in:
   - `tmpfs_read_pages`
   - `tmpfs_read` (dirent emission)
   - and raw user-source reads in `tmpfs_write_pages`

Fix applied:

- `tmpfs_read_pages`: use `copyout` for user destinations (`dst < KERNBASE`),
   retain direct `memmove`/`memset` only for kernel buffers.
- `tmpfs_write_pages`: use `copyin` for user sources (`src < KERNBASE`),
   retain direct `memmove` for kernel buffers.
- `tmpfs_read` dirent path: use `copyout` for user destination.

Status:

- Rebuilt successfully after tmpfs patch.
- Next rerun of `bcachestress -w 8 -r 200 -f 16 -k 32` is required to verify
   this closes the remaining immediate `cr2=0x12540` fault.

## Kernel-wide User-Copy Hardening Sweep (2026-04-05)

Goal:

- Apply the COW-safe user-copy rule across syscall-facing kernel paths before
   further debugging.

Rule applied:

- When a source/destination pointer is a user VA (`< KERNBASE`), do not use
   direct `memmove`/`memset`/byte writes.
- Use `copyout` for kernel-to-user writes and `copyin` for user-to-kernel
   reads, with process/pgdir checks.

Coverage completed in this sweep:

1. Core VM copy primitive
    - `kernel/core/vm.c`: `copyout` now resolves COW pages (`cow_fault`) and
       rejects non-writable/non-COW mappings.

2. Core file/inode/proc paths
    - `kernel/fs/fs.c`: `readi`/`writei` user-pointer handling moved to
       `copyout`/`copyin`.
    - `kernel/fs/procfs.c`: `procfs_copy_data` user destination path uses
       `copyout`.
    - `kernel/fs/file.c`: mount-root synthetic `.` and `..` dirent emission uses
       `copyout` for user buffers.

3. IPC/device syscall paths
    - `kernel/core/pipe.c`: `piperead`/`pipewrite` now use `copyout`/`copyin`
       for user buffers.
    - `kernel/driver/pty.c`: `pty_fileread`/`pty_filewrite` use staged kernel
       buffers plus `copyout`/`copyin` for user buffers.
    - `kernel/driver/tuntap.c`: `tuntap_fileread`/`tuntap_filewrite` use
       `copyout`/`copyin` for user buffers.
    - `kernel/core/blockdev.c`: `blockdev_read`/`blockdev_write` use
       `copyout`/`copyin` for user buffers.

4. Filesystem backend paths reached via VFS
    - `kernel/fs/vfs_tmpfs.c`: file reads/writes and dirent emission now handle
       user buffers via `copyout`/`copyin`.
    - `kernel/fs/vfs_ext2.c`: backend device read/write and sparse zero-fill
       reads now handle user buffers via `copyout`/`copyin`.
    - `kernel/fs/vfs_msdosfs.c`: file reads/writes and dirent emission now use
       `copyout`/`copyin` for user buffers.
    - `kernel/fs/vfs_ufs2.c`: backend reads, zero-fill reads, and dirent
       emission now use `copyout` for user buffers.
    - `kernel/fs/vfs_isofs.c`: file reads and dirent emission now use `copyout`
       for user buffers.
    - `kernel/fs/vfs_btrfs.c`: dirent emission now uses `copyout` for user
       buffers.

Build status after sweep:

- `aux.kern` and `_bcachestress` rebuilt successfully.

Scope note:

- This sweep focused on syscall-facing user-copy surfaces and VFS backends
   involved in process I/O. Internal kernel-only memory copies (for example
   framebuffer blits or purely in-kernel struct copies) were intentionally left
   unchanged.

## New Panic After Copy Sweep: kpage_drop_ref Underflow (2026-04-05)

Observed under stress:

```text
bcachestress: round 10/200 ok
lapicid 0: panic: kpage_drop_ref: kpage_drop_ref underflow
```

Interpretation:

- This indicates a duplicate free/unmap path reached `kfree()` with a managed
   page already at `refcount==0`.
- After the user-copy hardening sweep, this became the next dominant failure
   mode under fork/COW churn.

Mitigation applied:

- `kernel/core/kalloc.c::kfree` now explicitly checks for
   `meta->refcount==0 && (meta->flags & KPAGE_FREE)` and treats it as a
   duplicate free event.
- In that case it logs:
   - `kfree: duplicate free ignored pa=... v=...`
   and returns early (no panic, no second freelist insertion).
- If `refcount==0` but `KPAGE_FREE` is not set, kernel still panics with an
   explicit state message (`kfree refcount state`) because that indicates a
   deeper metadata inconsistency.

Why this is safe:

- The old behavior panicked in `kpage_drop_ref` underflow.
- Blindly continuing into normal free path would enqueue the same page twice and
   corrupt allocator freelists.
- Early return on known duplicate-free state preserves allocator integrity while
   allowing stress to continue and expose remaining root causes.

Build status:

- Rebuilt successfully after this mitigation.

## New Crash Signature: kfree Local-Cache Drain Fault (2026-04-06)

Observed under the same stress command:

```text
bcachestress -w 8 -r 200 -f 16 -k 32
...
unexpected trap 14 from cpu 0 eip 80140912 (cr2=0x9fc00000)
FATAL trap: kernel-page-fault cpu=0x00000000 trap=0x0000000e err=0x00000002 eip=0x80140912
```

Symbol mapping (`kernel.asm`):

- `0x80140912` is `kfree+0x12e`
- Faulting instruction: `mov %eax,(%edx)`
- This is the write to `p->next` while draining a per-CPU `kfree_cache` entry
   into `kmem.freelist`.

Interpretation:

- The popped cache entry pointer (`p`) was stale/corrupted by the time it was
   consumed.
- Earlier checks only validated virtual address shape (alignment/range), not
   allocator metadata state (`managed`, `free`, `refcount==0`).
- Under duplicate-free/unmap races, this allowed stale cache entries to survive
   long enough to fault at dereference time.

Hardening applied:

1. Added strict free-run validation in `kernel/core/kalloc.c`:
      - `kalloc_free_run_valid()` now requires:
         - page-aligned/range-valid pointer,
         - `KPAGE_MANAGED` set,
         - `KPAGE_FREE` set,
         - `refcount == 0`.
2. Added `kalloc_cache_pop_valid()` for per-CPU cache consumption:
      - invalid/stale cache entries are dropped instead of dereferenced,
      - drop count is tracked in `kmem.invalid_cache_drops`,
      - periodic diagnostic log is emitted.
3. Updated all local-cache pop/drain sites to use strict validation:
      - `kalloc()` fast-path cache pop,
      - `kalloc()` refill pop,
      - `kalloc_drain_local()` push-to-global path,
      - `kfree()` emergency drain path.
4. Tightened freelist pop validation in `kalloc_refill_local()` and early
    `kalloc()` path to use metadata-aware checks.

Build status:

- `sudo make aux.kern _bcachestress` succeeded on macOS host.

Next verification target:

- Re-run `bcachestress -w 8 -r 200 -f 16 -k 32` and check whether:
   - crash is eliminated,
   - or allocator now reports invalid cache drops before any fault,
   which would further localize the producer path for stale entries.

## New Crash Signature: filewrite Entry Dereference (2026-04-06)

Observed under the same stress command:

```text
bcachestress -w 8 -r 200 -f 16 -k 32
unexpected trap 14 from cpu 0 eip 8010ffa0 (cr2=0x0780200c)
FATAL trap: kernel-page-fault cpu=0 trap=0x0e err=0x00000000 eip=0x8010ffa0
```

Symbol mapping (`kernel.asm`):

- `0x8010ffa0` is in `filewrite()` at the first field access:
   `cmpb $0x0, 0x9(%ebx)` (`f->writable`).

Interpretation:

- Kernel faulted before any inode/device path work.
- `cr2=0x0780200c` is below `KERNBASE`, indicating the `struct file *f`
   argument was invalid/corrupted by the time `filewrite()` dereferenced it.
- This aligns with lifecycle/handle integrity problems at the API boundary,
   not allocator throughput itself (consistent with `kallocstress` passing).

Hardening applied:

1. Added a file-object magic tag:
      - `include/file.h`: new `struct file::magic` and `FILE_MAGIC`.
2. File lifecycle now stamps/clears magic:
      - `kernel/fs/file.c::filealloc` sets `magic = FILE_MAGIC`.
      - `kernel/fs/file.c::fileclose` clears magic before free.
3. Added `file_handle_valid()` in `kernel/fs/file.c` and guarded entry points:
      - `filedup`, `fileclose`, `filestat`, `fileread`, `filewrite`.
      - Invalid handles now fail safely (`-1` or no-op close) instead of
         blindly dereferencing and faulting.
4. Hardened syscall fd lookup:
      - `kernel/core/sysfile.c::argfd` now rejects file pointers below
         `KERNBASE` and rejects non-live file objects (`magic` mismatch or
         `ref < 1`).

Build status:

- `sudo make aux.kern _bcachestress` succeeded after this patch set.

Next verification target:

- Re-run `bcachestress -w 8 -r 200 -f 16 -k 32` and capture either:
   - successful completion progression, or
   - new explicit failure location (if corruption now surfaces elsewhere).
## New Crash Signature: kfree Drain Write to Unmapped KVA (2026-04-06)

Observed under the same stress command:

```text
bcachestress -w 8 -r 200 -f 16 -k 32
unexpected trap 14 from cpu 0 eip 80140acd (cr2=0x9fc00000)
FATAL trap: kernel-page-fault cpu=0 trap=0x0e err=0x00000002 eip=0x80140acd
```

Symbol mapping (`kernel.asm`):

- `0x80140acd` is in `kfree()` while draining per-CPU cache into global
  freelist (`p->next = kmem.freelist`).

Interpretation:

- This is a write fault to a non-present page at a would-be free-page KVA.
- Prior checks validated pointer shape and allocator metadata but did not verify
  that the pointer is writable in the currently active page table.
- Under page-table corruption or stale metadata, a pointer can still pass shape
  checks yet fault at first dereference.

Hardening applied:

1. Added VM helper in `kernel/core/vm.c`:
    - `kaddr_writable_current_pgdir(char *kva)`
    - checks mapping presence and `PTE_W` in the active pgdir (`myproc()->pgdir`
      when present, else `kpgdir`).
2. Exported the helper in `include/defs.h`.
3. Strengthened `kernel/core/kalloc.c::kalloc_free_run_valid()` to require
   `kaddr_writable_current_pgdir((char*)r)` before accepting a free-run pointer.

Build status:

- `sudo make aux.kern _bcachestress` succeeded after this patch.

Next verification target:

- Re-run `bcachestress -w 8 -r 200 -f 16 -k 32`.
- If a new panic appears, capture `eip` and `cr2`; this guard should prevent
  this exact non-present write fault in `kfree` drain path.

## April 6 Deep Audit Update: kmalloc_free Loop-Bound Corruption (2026-04-06)

Observed repro:

```text
bcachestress -w 8 -r 200 -f 16 -k 32
kfree: duplicate free ignoring pa=... caller=80141309
unexpected trap 14 ... eip 80140af5 (cr2=0x9fc00000)
```

Address mapping from `kernel.asm`:

- `0x80141309` -> `kmalloc_free` loop increment (`inc %edi`) in the page-free loop.
- `0x80140af5` -> `kfree` drain path write (`p->next = kmem.freelist`).

What this proves:

1. Duplicate-free reports are coming from the `kmalloc_free` loop path.
2. Crash occurs when a popped cached run pointer is first dereferenced in `kfree` drain.

Deterministic logic bug identified in `kmalloc_free`:

- The loop bound used `h->npages` directly on each iteration.
- The first `kfree(base)` can make the header page reusable.
- If that page is recycled before loop completion, `h->npages` can change mid-loop.
- Result: over-freeing arbitrary pages, duplicate-free storms, and allocator state poisoning.

Fix applied in `kernel/core/kmalloc.c`:

1. Snapshot header fields before any free:
   - `npages = h->npages`
   - `req_size = h->req_size`
2. Validate and log using the snapshotted locals.
3. Free with stable bound:
   - `for(i = 0; i < npages; i++) kfree(base + i * PGSIZE);`

Build verification:

- `sudo make aux.kern` succeeded after this fix.

Why this is not a guess:

- The caller address in logs (`0x80141309`) lands exactly on the kmalloc_free loop site.
- The fix removes a concrete use-after-free-of-header pattern in loop control logic.
- This is sufficient to generate exactly the observed duplicate-free cascades.

## Validation: bcachestress PASS After kmalloc_free Fix (2026-04-06)

User-verified repro result after the `kmalloc_free` loop-bound snapshot fix:

```text
bcachestress -w 8 -r 200 -f 16 -k 32
bcachestress: round 10/200 ok (worker_errs_so_far=0)
...
bcachestress: round 200/200 ok (worker_errs_so_far=0)
bcachestress: PASS 200 rounds, 8 workers, 16 files each
```

No panic signatures observed in this run:

- no `kfree: duplicate free ... caller=80141309`
- no trap-14 at `0x80140af5`
- no allocator drain write fault with `cr2=0x9fc00000`

Round-200 allocator/memory telemetry captured from the successful run:

```text
pages_total 129556
pages_free 128474
pages_allocated 1082
pages_shared 0
alloc_calls 852278
free_calls 1029123
cache_alloc_hits 852125
cache_alloc_misses 0
cache_free_inserts 851190
global_refill_batches 37973
global_refill_pages 607568
global_drain_batches 37913
global_drain_pages 606608
ref_increments 48371
deferred_frees 48371

MemTotal: 518224 kB
MemFree: 513892 kB
PagesTotal: 129556
PagesFree: 128473
PagesAlloc: 1083
```

Conclusion:

- The `kmalloc_free` header reuse bug was a real root-cause contributor.
- Snapshotting `npages/req_size` before the first free removed the loop-control
   use-after-free condition and stabilized the stress path.
- Current status for this failure mode: **resolved by validated repro**.