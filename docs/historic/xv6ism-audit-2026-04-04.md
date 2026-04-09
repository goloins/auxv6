# auxv6 xv6ism Audit (2026-04-04)

## Scope and method

This audit focused on high-confidence, code-backed leftovers of xv6-era behavior that are now outgrown by auxv6 architecture/policy.

Search and review covered:
- Build system and image tooling
- Kernel VFS/filesystem dispatch and rootfs init paths
- Syscall paths with backend-specific fallbacks
- Userspace mount helper staging

Low-confidence style/history mentions (for example "xv6-derived" in historical docs) are intentionally excluded from the action list.

## High-confidence leftovers

### 1) Legacy xv6fs backend still in the default kernel build

Current evidence:
- `kernel/fs/vfs_xv6fs.c` remains a full backend implementation.
- `Makefile` still links `kernel/fs/vfs_xv6fs.o` into `aux.kern`.
- `kernel/fs/vfs.c` rootfs selection still supports `ROOTFS_TYPE_XV6FS`.

Why this is antiquated:
- Current policy and roadmap are ext2-root-first and treat xv6fs as deprecated.
- Keeping xv6fs as a first-class default path forces cross-layer conditionals and maintenance cost.

### 2) Build system still ships xv6 image/tooling path (`mkfs`, `xv6memfs.img`)

Current evidence:
- `Makefile` target `mkfs: tools/mkfs.c include/fs.h` builds legacy mkfs.
- `Makefile` target `xv6memfs.img` and `qemu-memfs` remain active.
- `Makefile` clean list still includes `xv6memfs.img` and `mkfs` artifacts.
- `.gitignore` still tracks `mkfs` as a generated artifact.
- `tools/mkfs.c` is still in-tree and buildable.

Why this is antiquated:
- Project directive explicitly says not to use xv6 mkfs; ext2/host tooling is the supported path.
- Keeping runnable targets invites accidental use of obsolete image formats/workflows.

### 3) Rootfs type config still models xv6fs as peer root mode

Current evidence:
- `include/rootfs_config.h` generated constants include `ROOTFS_TYPE_XV6FS` and `ROOTFS_TYPE_EXT2`.
- `include/file.h` duplicates rootfs type constants before including generated config.

Why this is antiquated:
- The project has moved to ext2-root default and broad VFS backends; compile-time xv6-vs-ext2 root mode is legacy coupling.
- Duplicate macro ownership in two headers is brittle and easy to desynchronize.

### 4) Runtime code still branches on "is xv6fs" in generic paths

Current evidence:
- `include/vfs.h` exports `vfs_dev_is_xv6fs()`.
- `kernel/fs/vfs.c` implements `vfs_dev_is_xv6fs()`.
- `kernel/fs/fs.c` (`iput`) keeps a special xv6fs truncate/free path when no backend drop op exists.
- `kernel/core/sysfile.c` (`create`) falls back to legacy xv6 inode/dir primitives.
- `kernel/core/sysfile.c` mount dispatch still accepts explicit `xv6fs` with `ROOTDEV` default.

Why this is antiquated:
- Filesystem-specific logic leaks into generic VFS/syscall layers.
- New filesystems are expected to advertise capabilities/ops instead of being special-cased by name.

### 5) Transaction/log initialization remains xv6fs-root conditional

Current evidence:
- `kernel/fs/log.c` no-ops `begin_op()`/`end_op()` when rootfs type is not xv6fs.
- `kernel/core/proc.c` `forkret()` conditionally runs `iinit()`/`initlog()` only for xv6fs root.

Why this is antiquated:
- Rootfs-type compile-time checks in core lifecycle paths are an xv6 carryover.
- Modern multi-backend VFS should route transaction semantics through backend capabilities/hooks.

### 6) Userspace still stages xv6fs mount helper in target rootfs

Current evidence:
- `targetfs/sbin/mount.xv6fs` exists and execs `/bin/mount ... xv6fs`.
- `Makefile` `ROOTFS_COMMON_FILES` still installs `targetfs/sbin/mount.xv6fs`.

Why this is antiquated:
- It advertises a deprecated backend in the default image surface.
- This conflicts with ext2-first policy and increases support surface area.

## Recommended migration plan

## Decision gate (required first)

Pick one policy explicitly:
- A) Full retirement: remove xv6fs from default build/runtime and tooling.
- B) Quarantine: keep xv6fs only behind an explicit legacy build flag (off by default).

Without this decision, cleanup will oscillate.

## Phase 1: Build/tooling de-risk (low runtime risk)

1. Remove accidental entry points to obsolete tooling.
2. Keep runtime behavior unchanged in this phase.

Concrete changes:
- Remove `mkfs` build target and associated clean/.gitignore references.
- Remove `xv6memfs.img` and `qemu-memfs` targets from default Makefile.
- Keep `tools/mkfs.c` only if policy is quarantine; otherwise delete it.
- Remove `targetfs/sbin/mount.xv6fs` from staged rootfs list.

Validation:
- `make aux.kern` still succeeds.
- ext2-root boot path unchanged.
- No docs or scripts mention deprecated mkfs workflow as an active path.

## Phase 2: Header/config cleanup

1. Eliminate duplicate rootfs type macro ownership.
2. Move from rootfs-type enum coupling toward backend registration policy.

Concrete changes:
- Stop defining rootfs type constants in `include/file.h`; keep a single owner.
- If policy A: remove `ROOTFS_TYPE_XV6FS` from generated config.
- If policy B: gate `ROOTFS_TYPE_XV6FS` behind `CONFIG_LEGACY_XV6FS`.

Validation:
- Full rebuild with no macro redefinition risks.
- Rootfs config remains deterministic and documented.

## Phase 3: VFS/syscall de-specialization

1. Remove name-based xv6 checks from generic paths.
2. Use vnode ops/capabilities as the contract.

Concrete changes:
- Remove `vfs_dev_is_xv6fs()` export and implementation.
- In `iput`, replace xv6 fallback with capability/ops-driven behavior:
  - preferred: backend `drop` op
  - fallback: explicit generic helper only when semantically valid and capability-declared
- In `create`, remove legacy fallback logic; require `create`/`dirlookup`/`dirlink` ops (or documented capability path).
- In `sys_mount`, remove `xv6fs` type acceptance (policy A) or compile-gate it (policy B).

Validation:
- `fsregress` + `fatregress` + `mounttest` continue to pass on ext2/msdosfs/exfat/btrfs/ufs2/isofs/tmpfs/nfs paths as applicable.
- No generic syscall path branches on backend name strings.

## Phase 4: Lifecycle/log path modernization

1. Decouple init/logging from compile-time rootfs type.
2. Keep behavior explicit for each backend.

Concrete changes:
- Replace `ROOTFS_TYPE_XV6FS` checks in `begin_op`/`end_op` with backend capability checks or noop wrappers selected by mounted root backend.
- Remove `forkret` rootfs-type branching; move any required init calls into backend bring-up paths.

Validation:
- Boot/init path remains stable under ext2 default.
- No regressions in filesystem mutation paths that rely on transactions.

## Phase 5: Docs and policy lock-in

1. Update docs so old paths are not reintroduced.
2. Add a small CI/grep guard against regressions.

Concrete changes:
- Update roadmap and developer directives with final policy (retired vs quarantined).
- Add a simple repo check (script or make lint target) for banned default-surface artifacts (`qemu-memfs`, `xv6memfs.img`, unguarded `xv6fs` root mode).

Validation:
- New contributors cannot discover obsolete xv6 workflows as default paths.

## Execution order summary

1. Phase 1 (build/tooling)
2. Phase 2 (header/config)
3. Phase 3 (VFS/syscall cleanup)
4. Phase 4 (lifecycle/log path)
5. Phase 5 (docs/guards)

This order minimizes boot-risk while removing the biggest accidental-footgun surfaces first.
