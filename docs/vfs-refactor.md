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

- keep `MOUNT_MAX` for now, but document it as a transitional limit
- isolate mount-table assumptions so later dynamic growth or namespaces are possible

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

### Stage 7: Retire Final xv6fs-isms From VFS

- audit root handling, lookup flow, helper naming, and dispatch paths for logic that only survives because xv6fs used to be the dominant model
- remove or isolate those assumptions from shared VFS code
- leave xv6fs either as a contained legacy backend or bring it up to the new mount-centric contract
- treat ext2 and msdosfs as the stronger reference points for conventional mounted filesystems

## Immediate Action Items

- implement a mount-scoped destroy contract in [include/vfs.h](include/vfs.h) and [kernel/fs/vfs.c](kernel/fs/vfs.c)
- convert ext2 and ext3 first because they are already actively evolving
- convert tmpfs early as the synthetic reference implementation
- convert exfat and nfs early because they already expose destroy/ownership bugs under the current contract
- audit shared VFS code for behavior that only exists to preserve xv6fs-era assumptions and mark each instance for removal or isolation
- record any backend that still depends on `fs->fs_data` or bootstrap globals as incomplete until those dependencies are removed

## Success Criteria

The refactor should be considered successful when:

- mount-private state is owned and freed only by the mount instance
- root and non-root mounts follow the same lifecycle rules
- no backend depends on VFS raw-freeing opaque state
- synthetic, network, and block-backed filesystems all fit the same lifecycle contract
- pathname semantics are primarily VFS-owned rather than backend-fragmented
- shared VFS code no longer carries special cases whose main purpose is preserving xv6fs-era behavior
- adding a new filesystem no longer requires bending the VFS around device-centric or legacy assumptions