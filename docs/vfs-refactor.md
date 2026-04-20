# VFS Refactor Plan
**Date**: April 20, 2026
**Goal**: Move auxv6 toward a BSD-like, mount-centric VFS contract so filesystem growth in both breadth and functionality does not keep tripping over legacy ownership, lifecycle, and dispatch constraints, and so the remaining xv6fs-era assumptions can finally be removed from the VFS core.

## Target Model

The refactor target should be BSD-like in the sense that mount instances, not filesystem-type descriptors, own mount-private state and teardown.

Desired properties:

- mount lifecycle is explicit and mount-scoped
- per-mount private state is owned by the mount instance, not hidden behind `struct vfs`
- VFS owns mount topology and pathname semantics; backends own on-filesystem lookup and object operations
- synthetic, network, readonly, and block-backed filesystems all fit the same lifecycle contract
- rootfs is a normal mount instance with limited special policy, not a distinct architecture
- remaining xv6fs-isms are treated as migration debt to remove, not compatibility constraints to preserve
- mounted filesystems are attached to an abstract storage provider and topology, not hard-wired to the assumption of one filesystem per raw physical device

This document is intentionally actionable. It describes the current deficiencies, the best migration path for each in-tree backend, and the additional VFS interfaces that should be modernized while this work is underway.

## Current Shape

The current design is a hybrid.

- Non-root mounts allocate a fresh `struct vfs` in [kernel/core/sysfile.c](kernel/core/sysfile.c) and register it through [kernel/fs/vfs.c](kernel/fs/vfs.c).
- The system root uses the static `rootvfs` object in [kernel/fs/vfs.c](kernel/fs/vfs.c).
- `mount_init` returns mount-private state through `m->fs_data`, but the VFS core also aliases that state into `fs->fs_data` in [kernel/fs/vfs.c](kernel/fs/vfs.c).
- `vfs_unmount()` calls the backend destroy hook and then also raw-frees `mount_fs_data` in [kernel/fs/vfs.c](kernel/fs/vfs.c).

That leaves auxv6 in an awkward middle state where `struct vfs` is acting as both a filesystem-type descriptor and a mount instance, depending on code path. A fair amount of that shape is historical xv6 carryover rather than a good fit for the broader filesystem set auxv6 now supports.

## Primary Refactor Goal

The first structural fix should be this:

- `struct vfs` becomes a backend/type descriptor only
- `struct mount` becomes the owner of mount-private state and lifecycle
- destroy becomes mount-scoped rather than `fs`-scoped
- VFS stops generically freeing backend-private state

If this is done first, ext3 replay work, synthetic filesystem work, and future backends all get a cleaner foundation, and the remaining xv6fs-style shortcuts in VFS can be retired instead of carried forward.

## Core Problems

### 1. Type And Instance Responsibilities Are Mixed

The current interface in [include/vfs.h](include/vfs.h) mixes mount-instance responsibilities into `struct vfs`:

- `void *fs_data`
- `void (*fs_destroy)(struct vfs *fs)`
- `int (*mount_init)(struct mount *m)`

That is the root of the current lifecycle ambiguity. A mount instance should own mount-private state. A backend descriptor should not also be the primary owner of instance memory.

### 2. Mount Ownership Is Ambiguous And Unsafe

Unmount currently does all of the following in [kernel/fs/vfs.c](kernel/fs/vfs.c):

- captures `mount_fs_data`
- calls `fs->fs_destroy(fs)`
- then raw-frees `mount_fs_data`

That is already unsafe:

- some backends free `fs->fs_data` in their destroy hook
- some do not
- ext2/ext3 mount state is now `kmalloc`-allocated rather than `kalloc`-allocated

The VFS layer should not be guessing allocator or ownership policy for backend-private memory.

### 3. Rootfs Is Over-Special-Cased

Root selection is compile-time in [kernel/fs/vfs.c](kernel/fs/vfs.c), root mount construction is bespoke there too, and several helper paths still treat root as a special world. This makes it harder to reason about mount behavior as a single model.

This is also one of the clearest surviving xv6fs-isms in the VFS shape: root handling still bleeds old single-filesystem assumptions into contracts that now need to support ext2, msdosfs, tmpfs, nfs, and future backends on equal footing.

The goal should be:

- root is still policy-special
- root is not lifecycle-special

### 4. Pathname Walk Is Too Distributed

The current contract asks each backend to implement full-string `namei` and `nameiparent` in [include/vfs.h](include/vfs.h), while VFS also performs its own mount-root `..` rewriting and mount crossover logic in [kernel/fs/vfs.c](kernel/fs/vfs.c).

That means pathname semantics are split across:

- VFS
- backend walkers
- backend-specific `..` behavior in some filesystems

This is fragile and hard to extend.

### 5. Device-Centric Dispatch Leaks Through The Whole Stack

Helpers such as:

- `vfs_dev_vops`
- `vfs_dev_fs_data`
- `vfs_dev_has_cap`

push callers toward treating `ip->dev` as the primary filesystem key. That is awkward for:

- synthetic filesystems such as procfs and tmpfs
- network filesystems such as nfs
- future abstract filesystems such as devfs or sysfs

BSD-like evolution wants mount- or vnode-owned dispatch, not device-number identity as the primary handle.

This is another place where xv6-style assumptions have outlived their usefulness. The more auxv6 grows real mount instances and non-local filesystems, the less acceptable it is for VFS structure to still privilege the old single-device worldview.

### 6. Capability Flags Are Too Naive

The static `VFS_CAP_*` bitset in [include/vfs.h](include/vfs.h) is copied into the mount slot and used as a hard gate, but it is not derived from mount flags or actual wired operations. Several backends already drift from their declared caps.

That means capability policy is not trustworthy enough to be architectural.

### 7. Static Global Mount Table Will Age Poorly

The mount registry is a fixed-size global array with `MOUNT_MAX` in [include/limits.h](include/limits.h). That is acceptable for now, but it should be documented as an intentional limitation, not an invisible architectural assumption.

Today this is not just an internal implementation detail. The fixed bound is baked into the VFS registry in [kernel/fs/vfs.c](kernel/fs/vfs.c), mirrored into the user-visible `mountinfo` ABI in [include/auxv6/user.h](include/auxv6/user.h), enforced in [kernel/core/sysfile.c](kernel/core/sysfile.c), and reused by procfs mount reporting in [kernel/fs/procfs.c](kernel/fs/procfs.c).

That means `MOUNT_MAX` already influences both kernel behavior and user-facing enumeration semantics. It exists today mainly because a fixed array is simple, xv6-style, allocation-free state management for a small kernel. That simplicity was reasonable early on, but it is now something to deprecate rather than design around.

Modern Unix-like systems generally do not model mount state this way. Older systems often had fixed global tables for mounts, files, or processes, but modern Linux and BSD kernels keep mount objects in dynamically allocated structures and global trees/lists, even if they still impose operational limits elsewhere. auxv6 should treat the static table as transitional legacy, not a target architecture.

### 8. Unmount Semantics Are Too Shallow

The current unmount path in [kernel/fs/vfs.c](kernel/fs/vfs.c):

- checks only direct device references
- does not treat subtree unmount as a first-class problem
- depends on backend destroy behavior being simple and synchronous

That will become brittle as filesystem complexity grows.

### 9. Backends Still Depend On Bootstrap Globals

Some backends still have active-device or bootstrap mount fallbacks so they can find a root inode without fully trusting mount-scoped state. This is a holdover from earlier designs and should be retired.

Those fallbacks should be treated as xv6fs-era migration debt, not as a pattern for new filesystem work.

### 10. The Vnode API Is Still Inode-Centric

`vnode_ops` in [include/vfs.h](include/vfs.h) operate directly on `struct inode *`. That is expedient, but it means the allegedly generic layer still assumes inode semantics everywhere.

For now this can stay, but it should be documented as a medium-term limitation.

## Filesystem-By-Filesystem Path Forward

### ext2

Files:

- [kernel/fs/vfs_ext2.c](kernel/fs/vfs_ext2.c)
- [kernel/fs/vfs_ext2_shared.h](kernel/fs/vfs_ext2_shared.h)
- [kernel/fs/ext_common.c](kernel/fs/ext_common.c)

Current state:

- uses `mount_init`
- destroy hook is a no-op
- mount-private state is now `kmalloc`-allocated in `ext2_mount_setup`
- still relies on bootstrap globals for root fallback

Best path forward:

- move ext2 mount teardown to a future mount-scoped destroy hook
- remove `ext2_active_dev` and `ext2_bootstrap_data` fallback behavior
- make `root_inode` depend only on mount-owned state
- keep ext-common and ext-journal-neutral helpers shared, but do not let ext2 depend on ext3-owned replay state

Priority: high

### ext3

Files:

- [kernel/fs/vfs_ext3.c](kernel/fs/vfs_ext3.c)
- [kernel/fs/ext_journal.c](kernel/fs/ext_journal.c)
- [kernel/fs/vfs_ext2_shared.h](kernel/fs/vfs_ext2_shared.h)

Current state:

- uses `mount_init`
- destroy hook is a no-op
- reuses ext2 mount data and ext2 read-side helpers
- increasingly wants richer mount-private replay state

Best path forward:

- make ext3 mount state explicitly mount-owned before introducing heap-owned replay buffers
- stop depending on `fs->fs_data` aliasing for ext3 runtime state
- keep ext3 journal state mount-scoped and replay-scoped
- once lifecycle is fixed, move from replay seed to payload capture and finally replay application

Priority: highest among filesystem users of the refactor

### msdosfs

Files:

- [kernel/fs/vfs_msdosfs.c](kernel/fs/vfs_msdosfs.c)

Current state:

- uses `mount_init`
- has no destroy hook
- still depends on bootstrap/global fallback patterns
- appears more mature functionally than its lifecycle model

Best path forward:

- add mount-scoped destroy once the VFS contract supports it
- remove bootstrap global lookup fallbacks
- audit caps versus actual wired vnode ops and readonly behavior
- keep it as the model for traditional DOS-family block filesystems under the new contract

Priority: high

### exfat

Files:

- [kernel/fs/vfs_exfat.c](kernel/fs/vfs_exfat.c)

Current state:

- uses `mount_init`
- currently frees `fs->fs_data` in its destroy hook
- therefore collides with generic unmount free behavior

Best path forward:

- move all exfat teardown into mount-scoped destroy
- stop freeing mount-private state through `fs->fs_data`
- keep allocator choice backend-owned
- audit capability declaration versus the actual operation table

Priority: highest among existing double-free candidates

### btrfs

Files:

- [kernel/fs/vfs_btrfs.c](kernel/fs/vfs_btrfs.c)

Current state:

- uses `mount_init`
- frees `fs->fs_data` in destroy
- capability declarations and wired operations are not yet fully aligned

Best path forward:

- move to mount-scoped destroy
- stop freeing mount-private state through `fs->fs_data`
- treat current backend as lifecycle-scaffolded first, feature-complete second
- align caps, mount flags, and vnode op exposure after the lifecycle refactor

Priority: high

### ufs2

Files:

- [kernel/fs/vfs_ufs2.c](kernel/fs/vfs_ufs2.c)

Current state:

- uses `mount_init`
- frees `fs->fs_data` in destroy
- capability declarations and actual op wiring need audit

Best path forward:

- move to mount-scoped destroy
- stop using `fs->fs_data` as the authoritative mount-private handle
- keep UFS2 as a conventional block-backed backend under the same contract as ext and FAT families

Priority: high

### isofs

Files:

- [kernel/fs/vfs_isofs.c](kernel/fs/vfs_isofs.c)

Current state:

- uses `mount_init`
- frees `fs->fs_data` in destroy
- still has singleton-style fallback state

Best path forward:

- remove singleton/global mount fallback
- move to mount-scoped destroy
- tighten readonly declaration so caps and ops match the intended model

Priority: medium-high

### tmpfs

Files:

- [kernel/fs/vfs_tmpfs.c](kernel/fs/vfs_tmpfs.c)

Current state:

- uses `mount_init`
- destroy currently tears down contents but relies on generic VFS to free the container
- otherwise already behaves more like a mount-scoped synthetic filesystem

Best path forward:

- convert tmpfs to the new mount-destroy hook early as a model synthetic backend
- keep mount-private state entirely in the mount instance
- use tmpfs as the proof that the new contract works equally well for non-device-backed filesystems

Priority: high and a good pilot backend

### procfs

Files:

- [kernel/fs/procfs.c](kernel/fs/procfs.c)

Current state:

- no mount init
- no destroy hook
- effectively stateless from a mount-private memory perspective

Best path forward:

- support an explicit no-op mount lifecycle under the new contract
- do not force procfs to synthesize fake mount-private state just to satisfy the interface
- use procfs to validate that the refactor does not assume block devices or private allocators

Priority: medium

### nfs

Files:

- [kernel/fs/vfs_nfs.c](kernel/fs/vfs_nfs.c)

Current state:

- uses `mount_init`
- destroy both tears down remote session state and frees `fs->fs_data`
- depends on device-style identity even though it is network-backed

Best path forward:

- move transport/session teardown into mount-scoped destroy
- stop coupling remote mount identity to `fs->fs_data`
- make it the primary test case for moving away from device-centric VFS assumptions
- revisit any backend-local `..` root behavior after pathname walk is centralized

Priority: highest among non-local filesystems

### xv6fs

Files:

- [kernel/fs/vfs_xv6fs.c](kernel/fs/vfs_xv6fs.c)

Current state:

- no mount init
- no destroy hook
- ignores `struct vfs *fs`
- hardcodes global xv6 root semantics

Best path forward:

- stop treating xv6fs compatibility as a reason to preserve VFS-wide special cases
- either quarantine xv6fs explicitly as a legacy shim with no influence on VFS contracts, or rewrite it fully as a mount-scoped backend
- remove any remaining root, device, or pathname assumptions in VFS that only still exist to accommodate xv6fs semantics
- prefer ext2/msdosfs-era behavior as the architectural baseline for conventional local filesystems, not xv6fs-era shortcuts

This backend should not drive the new contract. The refactor should actively shed VFS-level xv6fs-isms even if xv6fs itself remains as an intentionally legacy backend for a while.

Priority: medium as an architectural cleanup target, even if full backend rewrite remains lower priority

## Storage Stacking Constraints

The VFS refactor does not need to make md, RAID, dm-crypt-style, or other encrypted-volume support a primary goal right now, but it should avoid cementing assumptions that would force another refactor once those layers arrive.

Relevant design implications:

- a mounted filesystem may sit on top of a storage stack, not directly on a single physical disk
- mount identity should not depend on one raw `dev` number being the whole truth about the backing object
- storage transforms such as RAID assembly, integrity layers, or encryption should remain below the filesystem contract where possible
- VFS should care about mount topology, vnode dispatch, and mount options, not about whether the underlying block provider is direct, mirrored, striped, or decrypted
- any future storage-stack metadata or unlock/configuration arguments should have a cleaner home than ad hoc device-number conventions

The practical consequence for this document is that device-centric helpers and fixed global assumptions are even more important to retire. A future ext2, ext3, msdosfs, or other backend mounted on md or encrypted storage should still look like an ordinary mount instance to the VFS layer.

## Stubbed, Scaffolded, And Future Filesystem Families

Some in-tree backends are more scaffold than mature implementation from a VFS perspective, even if they expose many vnode operations. The refactor plan should treat lifecycle cleanup as a prerequisite for future feature work in:

- btrfs
- ufs2
- isofs
- parts of msdosfs/exfat

For future abstract filesystems such as theoretical devfs or sysfs:

- require no block-device assumptions in the mount contract
- allow stateless mounts or tiny mount-private state
- make VFS dispatch mount- or vnode-based rather than device-number-based

For future non-Unix-on-disk filesystems such as NTFS or VFAT variants:

- the same mount-scoped ownership model applies cleanly
- allocator policy must remain backend-owned
- mount options and encoding state should live on the mount, not on the backend descriptor

## Other VFS Interfaces To Modernize

### Section A: Replace `fs_destroy(struct vfs *)` With Mount-Scoped Teardown

Best path:

- add a mount destroy hook that receives `struct mount *`
- make VFS stop raw-freeing `mount_fs_data`
- let each backend own allocator choice and nested cleanup

This is the single most important refactor step.

### Section B: Split Filesystem Type From Mount Instance

Best path:

- treat `struct vfs` as immutable backend/type description
- treat `struct mount` as the instance
- stop storing authoritative mount state in `fs->fs_data`

This aligns best with the BSD-like goal.

### Section C: Rework Pathname Walk Around Component Lookup

Best path:

- centralize `..`, symlink following, and mount crossing in VFS
- shrink backend contracts toward component-level lookup and object operations
- stop requiring each backend to own full-string `namei`

This will simplify mount topology logic and reduce per-backend policy drift.

### Section D: Move Away From Device-Centric Dispatch

Best path:

- add a stable way to recover the owning mount from a live vnode/inode
- gradually replace `vfs_dev_*` consumers
- make synthetic and network backends equal citizens
- avoid assuming the backing store is a single raw device rather than a composed storage provider such as md or an encrypted volume layer

### Section E: Rework Capability And Mount Flag Policy

Best path:

- derive effective mutability from mount flags plus backend support
- reduce reliance on static `VFS_CAP_*` as the sole truth
- audit backends where caps and wired vnode ops already diverge

### Section F: Rationalize Rootfs Handling

Best path:

- keep root policy-specific where needed
- use the same mount lifecycle for root and non-root mounts
- eliminate backend bootstrap globals that exist only because root is special-cased
- remove root-path assumptions that persist mainly as xv6fs historical baggage

### Section G: Make Unmount Topology-Aware

Best path:

- explicitly handle descendant mounts
- define busy rules for subtree unmount
- ensure mount teardown happens only after the mount is detached from new lookups

### Section H: Revisit Static Limits And Global Tables

Best path:

- keep `MOUNT_MAX` for now only as a transitional limit
- isolate mount-table assumptions so later dynamic growth or namespaces are possible
- stop expanding ABI or procfs surfaces that hard-code the fixed mount-table size
- plan to replace the static global mount array with dynamically managed mount objects once lifecycle ownership is cleaned up

### Section I: Plan A Medium-Term Vnode API Cleanup

Best path:

- keep inode-centric vnode ops for now to limit scope
- document them as transitional
- move toward a clearer vnode/mount ownership model before adding more backend complexity

This should include auditing whether any inode or dispatch conventions remain mostly because xv6fs historically made them convenient rather than because the broader VFS still needs them.

### Section J: Formalize Mount Data And Mount Options

The current `mount.data` and `mount.datalen` pair is minimally typed and easy to misuse.

Best path:

- define a clearer contract for mount arguments/options
- make option parsing backend-owned but lifecycle-owned by the mount instance
- use this for future FAT/NTFS/NFS/tmpfs option growth

This is also the natural place to keep room for future storage-adjacent configuration that may accompany layered devices, such as unlock state, labels, provider names, or other non-filesystem mount context, without pushing those concerns into device-number hacks.

## Code-Level Audit And Refactor Plan

This section records the current code shape in enough detail to drive the first implementation passes.

### Audit Snapshot: VFS Core

Files:

- [include/vfs.h](include/vfs.h)
- [kernel/fs/vfs.c](kernel/fs/vfs.c)
- [kernel/core/sysfile.c](kernel/core/sysfile.c)

Observed facts:

- `struct vfs` still combines type-level data with mount-instance fields such as `fs_data` and `fs_destroy`.
- [kernel/fs/vfs.c](kernel/fs/vfs.c) stores the real mount registry in `static struct mount mounts[VFS_MOUNTS_MAX]`, but mount registration still aliases mount-private state into both `mounts[slot].fs_data` and `fs->fs_data`.
- `vfs_init()` builds root through the same general `mount_init` shape, but still keeps a separate static `rootvfs` object and root-specific wiring.
- `vfs_unmount()` removes a slot, drops the mountpoint, calls `fs->fs_destroy(fs)`, then raw-frees `mount_fs_data` and finally frees `fs` for non-root mounts. This is the current ownership hazard.
- `vfs_lookup()` and `vfs_lookup_parent()` still route path resolution through backend full-string `namei` and `nameiparent`, while VFS also injects mount-root `..` rewriting and mount-crossing behavior.
- `vfs_cross_into_mount()` and `vfs_mount_crossover()` still implement mount traversal by scanning the global mount table and matching `(dev, inum)` pairs.

Immediate conclusion:

- the first implementation stage should stabilize mount lifecycle and ownership without simultaneously rewriting lookup semantics
- mount registry internals should be hidden behind helpers before the static table is replaced

### Audit Snapshot: Mount Syscall Path

Files:

- [kernel/core/sysfile.c](kernel/core/sysfile.c)

Observed facts:

- `sys_mount()` heap-allocates one `struct vfs` per mounted instance, then initializes it by string-dispatch on filesystem type.
- device selection is still part policy, part backend default, part special-case allocator for tmpfs and nfs.
- mount data is staged as an opaque byte buffer and handed to `vfs_register_mount()`.
- `sys_mountinfo()` clamps requests to `VFS_MOUNTS_MAX`, so the static mount-table size has already escaped into syscall behavior.

Immediate conclusion:

- type registration and mount-instance creation should eventually be separated
- in the near term, the syscall path can keep string dispatch if the backend descriptor remains per-mount, but the lifecycle contract must become mount-scoped first
- tmpfs/nfs device allocation logic should be treated as temporary mount-identity policy, not as the long-term shape for abstract storage providers

### Audit Snapshot: Dev-Keyed Dispatch Surface

Files:

- [kernel/fs/file.c](kernel/fs/file.c)
- [kernel/fs/fs.c](kernel/fs/fs.c)
- [kernel/core/sysfile.c](kernel/core/sysfile.c)
- [kernel/core/exec.c](kernel/core/exec.c)
- [kernel/core/vm.c](kernel/core/vm.c)
- [kernel/driver/loop.c](kernel/driver/loop.c)

Observed facts:

- `vfs_dev_vops()` is used throughout file I/O, metadata syscalls, exec loading, VM segment loading, inode drop, and loop-device handling.
- `vfs_dev_has_cap()` is used as a write/read/create gate in common file paths.
- `vfs_dev_fs_data()` is used by multiple backends as their primary way to recover mount-private state from `ip->dev`.
- `vfs_dev_is_xv6fs()` still gates fallback behavior in generic inode and create/remove paths.

Immediate conclusion:

- this surface is too wide to replace in one shot
- the refactor needs a compatibility stage where new mount-based helpers exist alongside the current `vfs_dev_*` helpers
- generic code should move first to inode- or vnode-based helpers, while backend-local `data_for_dev()` helpers are converted afterward

### Audit Snapshot: Backend Ownership Patterns

Representative files:

- [kernel/fs/vfs_ext2.c](kernel/fs/vfs_ext2.c)
- [kernel/fs/vfs_ext3.c](kernel/fs/vfs_ext3.c)
- [kernel/fs/vfs_tmpfs.c](kernel/fs/vfs_tmpfs.c)
- [kernel/fs/vfs_exfat.c](kernel/fs/vfs_exfat.c)
- [kernel/fs/vfs_nfs.c](kernel/fs/vfs_nfs.c)
- [kernel/fs/vfs_msdosfs.c](kernel/fs/vfs_msdosfs.c)
- [kernel/fs/vfs_btrfs.c](kernel/fs/vfs_btrfs.c)
- [kernel/fs/vfs_ufs2.c](kernel/fs/vfs_ufs2.c)
- [kernel/fs/vfs_isofs.c](kernel/fs/vfs_isofs.c)
- [kernel/fs/vfs_xv6fs.c](kernel/fs/vfs_xv6fs.c)

Observed facts:

- ext2/ext3 already receive `struct mount *` during init, but runtime lookup still depends on `fs->fs_data` and bootstrap fallbacks.
- tmpfs destroy tears down the tree via `fs->fs_data`, but generic VFS still frees the container.
- exfat, btrfs, ufs2, isofs, and nfs currently free mount state from `fs->fs_data` in destroy hooks.
- msdosfs has no destroy hook yet, but still recovers state through `vfs_dev_fs_data(dev)`.
- xv6fs and procfs have no mount lifecycle and remain the main sources of “stateless special-case” assumptions.

Immediate conclusion:

- ext2/ext3/tmpfs/exfat/nfs are the right first conversion set because they expose the most important ownership and mount-state issues
- msdosfs should follow soon after because it is one of the primary non-xv6 local filesystems and still uses dev-keyed state recovery
- btrfs/ufs2/isofs can follow once the mount-destroy and mount-state access pattern is stable

## Detailed Staged Execution Plan

### Stage 1: Introduce A Mount-Scoped Lifecycle Contract

Primary files:

- [include/vfs.h](include/vfs.h)
- [kernel/fs/vfs.c](kernel/fs/vfs.c)
- [kernel/core/sysfile.c](kernel/core/sysfile.c)

Concrete changes:

- replace `void (*fs_destroy)(struct vfs *fs)` with a mount-scoped destroy hook, for example `void (*mount_destroy)(struct mount *m)`
- keep `mount_init(struct mount *m)` as the creation hook
- make `struct mount` the only authoritative owner of mount-private state
- stop having VFS generically free `mount_fs_data`
- keep freeing dynamically allocated per-mount `struct vfs` objects in VFS for now, since `sys_mount()` still allocates them
- add one internal helper to clear/free a mount slot so slot teardown becomes explicit and reusable

Compatibility rule:

- for one transition stage, VFS may still mirror `m->fs_data` into `fs->fs_data` for old backends, but that mirror should be documented as compatibility-only and removed in Stage 3

First backend conversions:

- ext2
- ext3
- tmpfs
- exfat
- nfs

Reason:

- these backends either already behave like mount-scoped instances or they currently demonstrate the destroy/ownership bug most clearly

### Stage 2: Add Mount-Based Dispatch Helpers Without Removing The Old Ones

Primary files:

- [include/vfs.h](include/vfs.h)
- [include/defs.h](include/defs.h)
- [kernel/fs/vfs.c](kernel/fs/vfs.c)

Concrete changes:

- add helpers that recover mount-facing state from a live inode or vnode rather than from raw `dev` alone
- candidates include helpers such as `vfs_inode_mount(ip)`, `vfs_inode_vops(ip)`, `vfs_inode_has_cap(ip, cap)`, and `vfs_inode_fs_data(ip)`
- internally, those helpers may still use the current dev-based lookup where necessary, but callers stop depending on that detail
- keep existing `vfs_dev_*` helpers as wrappers during this stage so the tree remains buildable while call sites convert incrementally

First generic call sites to convert:

- [kernel/fs/file.c](kernel/fs/file.c)
- [kernel/core/exec.c](kernel/core/exec.c)
- [kernel/core/vm.c](kernel/core/vm.c)
- [kernel/fs/fs.c](kernel/fs/fs.c)
- [kernel/core/sysfile.c](kernel/core/sysfile.c)

Reason:

- these files contain the highest-value shared paths and most of the `vfs_dev_*` surface area

### Stage 3: Remove `fs->fs_data` As The Runtime Source Of Truth

Primary files:

- [kernel/fs/vfs_ext2.c](kernel/fs/vfs_ext2.c)
- [kernel/fs/vfs_ext3.c](kernel/fs/vfs_ext3.c)
- [kernel/fs/vfs_tmpfs.c](kernel/fs/vfs_tmpfs.c)
- [kernel/fs/vfs_msdosfs.c](kernel/fs/vfs_msdosfs.c)
- [kernel/fs/vfs_exfat.c](kernel/fs/vfs_exfat.c)
- [kernel/fs/vfs_btrfs.c](kernel/fs/vfs_btrfs.c)
- [kernel/fs/vfs_ufs2.c](kernel/fs/vfs_ufs2.c)
- [kernel/fs/vfs_isofs.c](kernel/fs/vfs_isofs.c)
- [kernel/fs/vfs_nfs.c](kernel/fs/vfs_nfs.c)

Concrete changes:

- convert backend-local `data_for_dev()` helpers to use the new mount-facing helpers
- stop reading mount state from `fs->fs_data` inside backend runtime operations
- remove ext2 bootstrap/global fallbacks once root mount state is reliably mount-owned
- convert tmpfs to free its mount container in mount destroy, not half in backend and half in VFS
- convert readonly backends that currently free `fs->fs_data` directly to the new mount destroy hook

Completion condition:

- `fs->fs_data` is either removed entirely or left unused long enough to delete confidently

### Stage 4: Quarantine And Remove xv6fs-Specific Generic Fallbacks

Primary files:

- [kernel/fs/fs.c](kernel/fs/fs.c)
- [kernel/core/sysfile.c](kernel/core/sysfile.c)
- [kernel/fs/vfs_xv6fs.c](kernel/fs/vfs_xv6fs.c)
- [kernel/fs/vfs.c](kernel/fs/vfs.c)

Concrete changes:

- remove generic branches that check `vfs_dev_is_xv6fs()` to decide whether fallback inode allocation, truncate, or create logic should run
- keep xv6fs operational only through its own backend ops, not through special behavior in generic VFS or inode code
- audit mount-root directory synthesis and root special-casing for logic that only exists because xv6fs historically lacked mount-scoped behavior

Completion condition:

- generic code does not need to know whether a filesystem is xv6fs in order to preserve correctness

### Stage 5: Hide The Static Mount Table Behind Stable Iteration And Lookup APIs

Primary files:

- [kernel/fs/vfs.c](kernel/fs/vfs.c)
- [kernel/fs/procfs.c](kernel/fs/procfs.c)
- [kernel/core/sysfile.c](kernel/core/sysfile.c)
- [include/auxv6/user.h](include/auxv6/user.h)

Concrete changes:

- keep the current array temporarily, but stop letting new code know about `VFS_MOUNTS_MAX`
- make procfs and `mountinfo` consume mount iteration helpers rather than relying on fixed-size shared arrays as the conceptual model
- ensure all table scans are confined to VFS internals so the eventual switch to dynamic mount objects touches fewer files

Completion condition:

- `MOUNT_MAX` remains only as a compatibility bound, not as a cross-subsystem design input

### Stage 6: Rework Lookup And Cross-Mount Semantics Around Mount Instances

Primary files:

- [kernel/fs/vfs.c](kernel/fs/vfs.c)
- [include/vfs.h](include/vfs.h)
- backend `namei` implementations as needed

Concrete changes:

- keep current string-based backend lookup until lifecycle and state ownership are stable
- then begin shrinking backend responsibilities toward component lookup instead of whole-path policy
- keep `..`, symlink following, mount crossing, and mount-root parent behavior in shared VFS code
- reduce or eliminate direct mount-table scans in crossover helpers where a more explicit mount/inode association becomes available

Reason:

- this is architecturally important, but it should follow ownership cleanup rather than compete with it

### Stage 7: Replace The Static Mount Registry

Primary files:

- [kernel/fs/vfs.c](kernel/fs/vfs.c)
- [include/vfs.h](include/vfs.h)
- [kernel/fs/procfs.c](kernel/fs/procfs.c)
- [kernel/core/sysfile.c](kernel/core/sysfile.c)

Concrete changes:

- replace `static struct mount mounts[VFS_MOUNTS_MAX]` with dynamically managed mount objects plus an internal list/tree
- keep external enumeration behavior stable while removing the fixed-table assumption internally
- revisit mount IDs or stable handles if later namespace or richer storage-stack work needs them

Reason:

- doing this after ownership and helper cleanup avoids coupling two high-risk refactors together

## Immediate Coding Order

If implementation starts now, the safest order is:

1. change the lifecycle contract in [include/vfs.h](include/vfs.h) and [kernel/fs/vfs.c](kernel/fs/vfs.c)
2. convert ext2, ext3, tmpfs, exfat, and nfs to mount-scoped destroy
3. add inode- or vnode-based helper wrappers in VFS while keeping `vfs_dev_*` compatibility
4. convert shared consumers in [kernel/fs/file.c](kernel/fs/file.c), [kernel/core/exec.c](kernel/core/exec.c), [kernel/core/vm.c](kernel/core/vm.c), [kernel/fs/fs.c](kernel/fs/fs.c), and [kernel/core/sysfile.c](kernel/core/sysfile.c)
5. convert msdosfs and the remaining block-backed backends away from `vfs_dev_fs_data()`
6. remove xv6fs-specific generic fallbacks
7. only then deprecate the fixed mount table in code rather than just in documentation

## Recommended Staged Execution Plan

### Stage 1: Fix Mount Ownership

- introduce mount-scoped destroy
- remove generic `kfree(mount_fs_data)` from VFS
- convert ext2, ext3, tmpfs, exfat, nfs first

### Stage 2: Remove `fs->fs_data` As Authoritative Mount State

- move all backend private state references to mount-owned state
- keep temporary compatibility shims only where strictly needed

### Stage 3: Eliminate Backend Bootstrap Globals

- ext2
- ext3/ext shared state
- msdosfs
- isofs
- any VFS-side fallback or root bootstrap logic that only survives for xv6fs-era reasons

### Stage 4: Rationalize Caps, Flags, And Unmount

- audit every backend’s caps against actual vnode ops
- make unmount subtree-safe
- make readonly semantics flow from mount state

### Stage 5: Centralize Pathname Semantics

- VFS owns `..`, mount crossing, and symlink policy
- backends own component lookup and object access

### Stage 6: Reduce Device-Centric Assumptions

- migrate synthetic and network filesystems first
- leave compatibility wrappers for block-backed backends until the caller surface is cleaned up

This stage should explicitly remove the remaining VFS assumptions that only make sense if xv6fs-style global or device-root semantics are still the mental model.

It should also leave the VFS able to treat md-backed or encrypted-volume-backed filesystems as ordinary mounts rather than special cases.

### Stage 7: Retire Final xv6fs-isms From VFS

- audit root handling, lookup flow, helper naming, and dispatch paths for logic that only survives because xv6fs used to be the dominant model
- remove or isolate those assumptions from shared VFS code
- leave xv6fs either as a contained legacy backend or bring it up to the new mount-centric contract
- treat ext2 and msdosfs as the stronger reference points for conventional mounted filesystems

### Stage 8: Deprecate The Fixed Mount Table

- stop treating `MOUNT_MAX` as an architectural constant outside temporary compatibility boundaries
- narrow the number of call sites that directly know about the global mount array
- move procfs and `mountinfo` enumeration toward mount iteration APIs that do not encode the table size into shared interfaces
- replace the static registry with dynamically managed mount objects once the mount lifecycle and lookup contracts are stable

## Immediate Action Items

- implement a mount-scoped destroy contract in [include/vfs.h](include/vfs.h) and [kernel/fs/vfs.c](kernel/fs/vfs.c)
- convert ext2 and ext3 first because they are already actively evolving
- convert tmpfs early as the synthetic reference implementation
- convert exfat and nfs early because they already expose destroy/ownership bugs under the current contract
- audit shared VFS code for behavior that only exists to preserve xv6fs-era assumptions and mark each instance for removal or isolation
- mark `MOUNT_MAX` and the static global mount registry as deprecated design debt, not a long-term contract
- avoid introducing new VFS interfaces that assume a mount is identified completely by one raw backing device number
- record any backend that still depends on `fs->fs_data` or bootstrap globals as incomplete until those dependencies are removed

## Success Criteria

The refactor should be considered successful when:

- mount-private state is owned and freed only by the mount instance
- root and non-root mounts follow the same lifecycle rules
- no backend depends on VFS raw-freeing opaque state
- synthetic, network, and block-backed filesystems all fit the same lifecycle contract
- pathname semantics are primarily VFS-owned rather than backend-fragmented
- shared VFS code no longer carries special cases whose main purpose is preserving xv6fs-era behavior
- the VFS no longer depends architecturally on a fixed global mount table
- a filesystem mounted on top of future md or encrypted storage can fit the same mount contract without new VFS special cases
- adding a new filesystem no longer requires bending the VFS around device-centric or legacy assumptions