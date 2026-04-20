# ext3 Bring-Up Checklist
**Date**: April 20, 2026  
**Objective**: Add ext3 support with metadata journaling while preserving ext2 behavior and code ownership boundaries.  
**Status**: Phase 0 design draft in progress

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

### Current Implementation Baseline

- [kernel/fs/vfs_ext2.c](/home/dakota/auxv6/kernel/fs/vfs_ext2.c#L2970) currently mounts ext-family images by checking the superblock magic, deriving block size, defaulting inode size when needed, and reading group descriptors.
- The current in-kernel ext2 superblock struct stops before the ext extended feature words, journal UUID/inode/dev fields, and orphan-head field, so ext2 mount code cannot yet distinguish plain ext2 from journal-requiring ext3 images.
- [kernel/core/sysfile.c](/home/dakota/auxv6/kernel/core/sysfile.c#L2216) currently dispatches only `ext2` and `ext2fs`; there is no distinct `ext3` or `ext3fs` mount entry point yet.
- Secondary-volume ext3 bring-up also exposed a host-filesystem pathwalk requirement: when a pathname crosses a mountpoint as an intermediate component, the owning walker for the host filesystem must hand off into the mounted root before continuing. In practice this surfaced in the ext2 walker for paths like `/mnt/ext3/docs` and `ext3/docs` from within `/mnt`.

### Phase 0 Output

Phase 0 should end with a mount-time decision layer that can answer, before any mutating path is used:

- whether the image is plain ext2-compatible for the current auxv6 ext2 backend
- whether the image requires ext3 journal semantics and therefore must be mounted only through the ext3 backend
- whether the image advertises features outside the first auxv6 ext3 subset and must be rejected

The output of this phase is a stable policy plus the superblock parsing needed to enforce it. It is not journal replay yet.

### ext3 On-Disk Feature Subset To Support Now

The first auxv6 ext3 target should be a conservative ext3 revision-1 style subset with an internal journal inode and ordered metadata journaling.

Supported baseline assumptions:

- dynamic revision superblock format with extended fields present
- valid ext superblock magic and block sizes already accepted by ext2 today: 1024, 2048, or 4096 bytes
- inode size at least 128 bytes and large enough to cover the ext extended inode fields already implied by revision 1 images
- internal journal only, identified by superblock journal inode metadata
- metadata journaling with ordered data writes
- ext2-style block mapping only: direct, singly indirect, and doubly indirect pointers as already modeled by the current backend

Explicitly out of scope for first ext3 enablement:

- external journal device support
- journal UUID attachment to another block device
- extents, 64-bit block counts, meta block groups, flex groups, huge files, and other ext4-era layout features
- hashed directory indexing as a required feature for mount success
- online resize as a required feature for mount success
- data=journal and writeback modes
- orphan-file style recovery features beyond the classic ext3 orphan-head model

### Required Superblock Fields For Phase 0

Extend the parsed ext superblock model to include, at minimum, these additional fields after the current ext2 subset:

- `s_feature_compat`
- `s_feature_incompat`
- `s_feature_ro_compat`
- `s_uuid`
- `s_volume_name`
- `s_last_mounted`
- `s_algorithm_usage_bitmap`
- `s_prealloc_blocks`
- `s_prealloc_dir_blocks`
- `s_journal_uuid`
- `s_journal_inum`
- `s_journal_dev`
- `s_last_orphan`

Phase 0 only needs enough of these fields to make mount decisions; later phases can add stricter validation and richer reporting.

### Mount Behavior Matrix

#### `ext2` or `ext2fs` mount request

- ext image with no journal-required semantics and no unsupported incompat bits: allow existing ext2 backend
- ext image with `has_journal` compat bit set but no journal-required incompat bits and no unsupported incompat bits: reject for now, do not silently mount as ext2
- ext image with any journal-requiring or otherwise unsupported incompat bit: reject
- ext image with unsupported read-only compatible bits on a writable mount: reject
- ext image with unsupported read-only compatible bits on a read-only mount: reject in Phase 0 for determinism unless we explicitly bless a bit as safe to ignore

#### `ext3` or `ext3fs` mount request

- ext image with internal journal metadata present, supported feature bits, and no unsupported incompat bits: allow ext3 backend
- ext image lacking journal metadata or lacking `has_journal`: reject, do not silently treat it as ext2
- ext image requiring external journal device support: reject
- ext image with unsupported incompat bits: reject
- ext image with unsupported read-only compatible bits: reject in first iteration unless explicitly approved as safe

#### No Silent Fallback Rule

- `ext3` mount never falls back to ext2 behavior
- `ext2` mount never treats a journaled image as plain ext2 just because the current ext2 code can parse the inode and block layout
- rejection happens during mount probing, before the filesystem becomes visible to VFS

### Initial Feature-Bit Policy

Phase 0 should encode the policy in terms of explicit allowlists and denylists rather than ad hoc checks spread across mount code.

Compat bits:

- allow: directory preallocation, imagic inodes, resize inode
- conditionally allow for ext3 only: `has_journal`
- reject for now: any unknown compat bit

Incompat bits:

- allow only the minimal set already required by current ext2 layout handling; expect `filetype` to be explicitly reviewed first because the current dirent parser already consumes the typed dirent layout
- reject for Phase 0: compression, needs_recovery until replay exists, journal_dev, meta_bg, extents, 64bit, flex_bg, inline data, casefold, encrypt, verity, and any unknown incompat bit

Read-only compatible bits:

- allow only bits that the selected backend can actually understand without changing write correctness; `sparse_super` is the first candidate to review because it affects metadata placement assumptions
- reject for Phase 0 by default: large file, btree dir, huge file, metadata checksum, quota, readonly snapshot, project quota, and any unknown ro-compat bit until individually reviewed

The important rule is conservative acceptance: if auxv6 does not have a clear argument that a bit is harmless for the selected backend, mount must fail.

### Deterministic Rejection Categories

Mount rejection should be driven by a small internal reason enum or equivalent single-source policy table so behavior is stable across ext2 and ext3 entry points.

Required rejection categories:

- bad magic or impossible base geometry
- unsupported block size or inode size
- missing required extended superblock fields for revision level
- ext3 requested but no usable internal journal metadata present
- ext2 requested on image advertising journal semantics
- unsupported compat bit
- unsupported incompat bit
- unsupported ro-compat bit
- external journal requested
- recovery-required image presented to a backend that cannot replay it

Even if the syscall surface only returns `-1`, kernel debug output and tests should key off stable rejection reasons.

### Mount Decision Rules To Lock In

- `has_journal` is treated as an ownership boundary: ext2 mount rejects it, ext3 mount requires it.
- `needs_recovery` is treated as a hard requirement for replay support: ext2 rejects it, ext3 rejects it until Phase 2 replay exists or mounts read-only probe-only by explicit design.
- `journal_dev` is a hard reject in both backends for the first iteration.
- unknown incompat bits are always a hard reject.
- unknown ro-compat bits are a reject in the first auxv6 ext3 iteration to avoid accidental semantic downgrade.

### Phase 0 Code Changes Expected

- extend the ext superblock definition in [kernel/fs/vfs_ext2.c](/home/dakota/auxv6/kernel/fs/vfs_ext2.c#L16) or, preferably, move the shared on-disk definitions into a new ext-common header once Phase 1 starts
- add a shared feature-validation helper that both ext2 and ext3 mount paths can call
- add a distinct `ext3` and `ext3fs` branch in [kernel/core/sysfile.c](/home/dakota/auxv6/kernel/core/sysfile.c#L2216) once the ext3 backend stub exists
- add mount tests that cover accepted and rejected feature combinations before journaling work begins

### Phase 0 Exit Criteria

- a single mount-decision helper can classify candidate images into ext2-allowed, ext3-allowed, or rejected
- ext2 mount rejects journaled or replay-required images deterministically
- ext3 mount rejects non-journaled images deterministically
- unsupported feature bits are rejected by policy, not by incidental downstream parse failure
- tests cover the full mount matrix and each rejection category
- intermediate-component mountpoint crossover remains correct for mounted ext3 secondary volumes; this is a path-resolution correctness constraint, not ext3 journal work

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
