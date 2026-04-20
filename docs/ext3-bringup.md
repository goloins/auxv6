# ext3 Bring-Up Checklist
**Date**: April 20, 2026  
**Objective**: Add ext3 support with metadata journaling while preserving ext2 behavior and code ownership boundaries.  
**Status**: Planned

---

## Scope And Principles

### Goals
- Add a dedicated ext3 filesystem backend selectable via mount fstype ext3/ext3fs.
- Keep ext2 stable and mountable exactly as today.
- Reuse ext2 code as implementation substrate where practical, but avoid creating a single merged ext2/ext3 backend that is hard to reason about.
- Deliver ext3 ordered mode first (metadata journal with ordered data writes).

### Non-Goals (First Iteration)
- ext4 feature support.
- Full JBD2 parity with Linux.
- External journal device support.
- Data=journal mode.

### Hard Separation Rules
- ext2 and ext3 expose separate init entry points and separate mount dispatch.
- ext3 transaction logic lives in ext3 files, not in ext2 files.
- Shared helpers are moved to a neutral ext-common module only when both backends need them.
- ext2 write paths never require journal state to execute.

---

## Current Code Anchors

- ext2 implementation and mutating paths: [kernel/fs/vfs_ext2.c](kernel/fs/vfs_ext2.c)
- mount dispatch in syscall layer: [kernel/core/sysfile.c](kernel/core/sysfile.c)
- vfs registration and mount metadata handling: [kernel/fs/vfs.c](kernel/fs/vfs.c)
- filesystem API surface: [include/vfs.h](include/vfs.h)
- existing xv6 log behavior (rootfs-type-gated): [kernel/fs/log.c](kernel/fs/log.c)
- ext2 fault injection and regressions: [user/fsregress.c](user/fsregress.c)

---

## Phase 0: Design Freeze And Safety Guards

### Checklist
- [ ] Document ext3 on-disk feature subset to support now.
- [ ] Define mount behavior matrix:
  - ext2 image + ext2 mount
  - ext3 image + ext3 mount
  - ext3 image + ext2 mount (must fail if journal-required bits are set)
- [ ] Extend superblock parsing model to include feature flags and journal metadata fields needed for ext3 detection and mount decisions.
- [ ] Add explicit unsupported-feature rejection list with deterministic errors.

### Acceptance Criteria
- All mount decisions are deterministic and testable.
- No silent fallback from ext3 semantics to ext2 semantics.

---

## Phase 1: Backend Split And Shared Core Extraction

### Checklist
- [ ] Create ext3 backend entry point and wiring:
  - add vfs_ext3_init declaration in [include/vfs.h](include/vfs.h)
  - add mount dispatcher branch in [kernel/core/sysfile.c](kernel/core/sysfile.c)
- [ ] Introduce new files for ext3 backend and ext-common helpers.
- [ ] Move purely shared, journal-agnostic helpers (inode decode, dirent validation, block mapping helpers) into ext-common module.
- [ ] Keep ext2 backend compiling and behavior-identical after extraction.

### Acceptance Criteria
- ext2 regression behavior remains unchanged.
- ext3 backend compiles and mounts only in read-only probe mode initially.

---

## Phase 2: ext3 Journal Core (Internal Journal Inode)

### Checklist
- [ ] Implement ext3 journal state object bound to mount fs_data.
- [ ] Implement mount-time journal discovery (internal journal inode).
- [ ] Implement replay path at mount:
  - load journal superblock
  - scan descriptor/commit records
  - apply committed transactions
  - clear/rearm journal tail
- [ ] Implement runtime transaction API:
  - begin
  - mark metadata buffer dirty-for-journal
  - commit
  - checkpoint/tail advance
- [ ] Add strict crash-safe ordering in journal commit sequence.

### Acceptance Criteria
- Recovery replays committed metadata changes after crash simulation.
- No replay of uncommitted transactions.

---

## Phase 3: Convert ext3 Mutating Operations To Transactions

### Checklist
- [ ] Wrap all metadata mutation sites in ext3 backend with transaction boundaries.
- [ ] Transaction-protect these metadata classes:
  - inode bitmap
  - block bitmap
  - group descriptors
  - superblock free counts and timestamps
  - inode table updates
  - directory entry updates
- [ ] Convert high-risk operations first:
  - create
  - unlink/remove
  - rename
  - truncate
  - symlink
  - write with allocation/growth
- [ ] Ensure failure paths do not leave journal or on-disk metadata in partial state.

### Acceptance Criteria
- ext3 metadata operations are atomic at transaction granularity.
- Existing ext2 fault-injection scenarios have ext3 analog tests.

---

## Phase 4: Ordered Data Mode

### Checklist
- [ ] Implement ordered mode policy for regular file writes:
  - data blocks are issued before metadata commit that exposes them
- [ ] Ensure directory and inode metadata references are not committed ahead of required data blocks.
- [ ] Add assertions and trace counters for ordering invariants in debug builds.

### Acceptance Criteria
- Post-crash, metadata points only to initialized data blocks under ordered mode assumptions.

---

## Phase 5: VFS Semantics, Mount Flags, And Userland UX

### Checklist
- [ ] Add ext3/ext3fs fstype support in mount syscall dispatch in [kernel/core/sysfile.c](kernel/core/sysfile.c).
- [ ] Add mount helper parity:
  - create [targetfs/sbin/mount.ext3](targetfs/sbin/mount.ext3)
- [ ] Add image creation support for ext3 journaled test images in tooling, likely extending [tools/stage-ext2-root.sh](tools/stage-ext2-root.sh) or adding a sibling script.
- [ ] Ensure mount read-only semantics are enforced in VFS write path capability checks in [kernel/fs/vfs.c](kernel/fs/vfs.c).

### Acceptance Criteria
- User can mount ext3 images with ext3 fstype and see expected behavior.
- Read-only mounts reject write paths consistently.

---

## Phase 6: Test Plan And Crash Harness

### Functional And Regression Checklist
- [ ] Add ext3 bring-up tests for:
  - mount/unmount cycle
  - create/write/read/unlink
  - mkdir/rmdir
  - rename same-dir and cross-dir
  - hardlink and symlink behavior
- [ ] Mirror ext2 fault tests with ext3-specific expected outcomes in [user/fsregress.c](user/fsregress.c).
- [ ] Add forced-failure injection points in ext3 transaction and commit pipeline.

### Crash Recovery Checklist
- [ ] Add deterministic crash injection at journal lifecycle points:
  - before commit record write
  - after commit record write, before checkpoint
  - during checkpoint copyback
- [ ] Boot/re-mount validation tests that assert expected recovered namespace and metadata state.

### Acceptance Criteria
- Repeatable crash tests pass with no orphaned metadata inconsistencies.
- ext2 test suite still passes with no regressions.

---

## Phase 7: Observability And Performance Hardening

### Checklist
- [ ] Add counters and state visibility (proc/debug output) for ext3:
  - tx begin/commit counts
  - replay count on mount
  - checkpoint cycles
  - commit latency histogram buckets (coarse)
- [ ] Add commit batching policy and tunables.
- [ ] Measure write-heavy benchmarks versus ext2 baseline.
- [ ] Identify and fix obvious lock contention in journal path.

### Acceptance Criteria
- ext3 write latency is acceptable for interactive workloads.
- Debug outputs make transaction behavior diagnosable.

---

## File Plan (Expected New Or Modified)

### New (Planned)
- [ ] kernel/fs/vfs_ext3.c
- [ ] kernel/fs/ext_journal.c
- [ ] kernel/fs/ext_journal.h
- [ ] kernel/fs/ext_common.c
- [ ] kernel/fs/ext_common.h
- [ ] targetfs/sbin/mount.ext3

### Modified (Planned)
- [ ] [include/vfs.h](include/vfs.h)
- [ ] [kernel/core/sysfile.c](kernel/core/sysfile.c)
- [ ] [kernel/fs/vfs.c](kernel/fs/vfs.c)
- [ ] [tools/stage-ext2-root.sh](tools/stage-ext2-root.sh) or sibling ext3 staging script
- [ ] [user/fsregress.c](user/fsregress.c)

---

## Milestone Gates

### Gate A: Safe Split
- ext3 backend exists, ext2 unchanged, ext3 mount detection and rejection behavior correct.

### Gate B: Journal Recovery
- mount-time replay works for committed metadata transactions.

### Gate C: Transactional Mutations
- create/remove/rename/truncate/write-allocation paths journaled.

### Gate D: Ordered Mode Correctness
- data-before-metadata ordering validated under crash tests.

### Gate E: User-Facing Integration
- mount ext3 works from userland tooling and docs.

### Gate F: Confidence
- crash harness and regression suite green; ext2 unaffected.

---

## Suggested Work Order (Implementation Sequence)

1. Gate A
2. Gate B
3. Gate C (create/remove first, then rename, then truncate/write growth)
4. Gate D
5. Gate E
6. Gate F

---

## Optional Reference Implementations To Consult

Not strictly required to start coding, but high-value for edge-case validation:
- Linux ext3 and jbd/jbd2 transaction and replay ordering behavior.
- NetBSD FFS journaling behavior and mount/recovery constraints.
- FreeBSD UFS soft updates/journaling interactions for failure-mode ideas.

If you want, I can pull a targeted reference checklist next (specific files/functions to inspect in Linux and BSD) so we have direct guidance for replay and commit ordering corner cases before coding starts.

---

## Reference Use And Licensing Guardrails

Use external references as design guidance, not as source text to copy.

### Rules
- Do not paste or adapt external code into this document or into auxv6 source files.
- Keep this document link-only for external projects.
- Record concepts, invariants, and failure-mode notes in original wording.
- If a behavior is influenced by a reference, capture only:
  - what behavior we want
  - why we want it
  - where in auxv6 it will be implemented
- Keep Linux/BSD naming out of auxv6 APIs unless there is a compatibility reason.

### Provenance Note Template
- Source URL:
- Upstream file/function:
- Behavior observed (one paragraph, no code):
- auxv6 decision:
- auxv6 target file(s):

---

## Link-Only Reference Backlog

### Linux (ext3/jbd)
- [ ] Journal on-disk layout and replay boundaries (descriptor/commit/revoke): https://www.kernel.org/doc/html/latest/filesystems/ext4/journal.html#layout
- [ ] Commit block semantics and replay validity checks: https://www.kernel.org/doc/html/latest/filesystems/ext4/journal.html#commit-block
- [ ] Revocation records and replay suppression rules: https://www.kernel.org/doc/html/latest/filesystems/ext4/journal.html#revocation-block
- [ ] Ordered mode behavior overview and data-vs-metadata guarantees: https://www.kernel.org/doc/html/latest/filesystems/ext4/journal.html
- [ ] jbd2 recovery implementation reference (read-only, no code copying): https://github.com/torvalds/linux/blob/master/fs/jbd2/recovery.c
- [ ] jbd2 transaction sequencing reference (read-only, no code copying): https://github.com/torvalds/linux/blob/master/fs/jbd2/transaction.c
- [ ] ext4 mount/superblock journal hookup reference: https://github.com/torvalds/linux/blob/master/fs/ext4/super.c
- [ ] ext4 namespace mutation path reference (rename/unlink touch points): https://github.com/torvalds/linux/blob/master/fs/ext4/namei.c

### NetBSD / FreeBSD
- [ ] NetBSD WAPBL journaling model and mount-time replay behavior: https://man.netbsd.org/wapbl.4
- [ ] NetBSD tunefs log sizing/removal workflow for in-filesystem journal: https://man.netbsd.org/tunefs.8
- [ ] NetBSD fsck_ffs invariants list (link counts, free map, directory structure): https://man.netbsd.org/fsck_ffs.8
- [ ] FreeBSD UFS tunefs journaling tradeoffs and operational caveats: https://man.freebsd.org/cgi/man.cgi?query=tunefs&sektion=8&manpath=FreeBSD+15.0-RELEASE+and+Ports.quarterly
- [ ] FreeBSD gjournal behavior and sync caveats (block-level journaling context): https://man.freebsd.org/cgi/man.cgi?query=gjournal&sektion=8&manpath=FreeBSD+15.0-RELEASE+and+Ports.quarterly
- [ ] FreeBSD handbook note on ext2fs driver limits (journaling unsupported in that path): https://docs.freebsd.org/en/books/handbook/filesystems/#filesystems-linux-ext
- [ ] FreeBSD fsck_ffs invariants and recovery model: https://man.freebsd.org/cgi/man.cgi?query=fsck_ffs&sektion=8&manpath=FreeBSD+15.0-RELEASE+and+Ports.quarterly

### Validation References
- [ ] Rename replay idempotence discussion (outcome-based replay model): https://www.kernel.org/doc/html/latest/filesystems/ext4/journal.html#fast-commit-replay-idempotence
- [ ] Journal checkpoint behavior and recovery boundary discussion: https://www.kernel.org/doc/html/latest/filesystems/ext4/journal.html#journal-checkpoint
- [ ] Commit/revoke interaction reference for metadata replay validation: https://www.kernel.org/doc/html/latest/filesystems/ext4/journal.html#revocation-block
