# UFS2 Driver (Read-Only, Initial Tranche)

## Progress Snapshot (2026-04)

Current completion estimate: ~28% of the long-term UFS2/FFS plan.

Landed now:
- Read-only VFS backend is in-tree and mountable via `ufs2` and `ffs` mount types.
- Superblock probe and mount-time geometry extraction are implemented.
- Inode and directory traversal are wired through VFS (`namei`, `nameiparent`, `dirlookup`).
- Regular-file reads support direct blocks and a first single-indirect path.
- Symlink reads are supported for both inline-short and block-backed forms.

Still missing for a broader milestone:
- Write path (`create`, `unlink`, `rename`, `truncate`, metadata updates).
- Double/triple-indirect read coverage.
- Wider on-disk layout compatibility validation across BSD variants.
- Robust malformed-metadata hardening and richer diagnostics.

## Scope

auxv6 now ships an initial read-only UFS2 backend through the VFS layer.

Supported in this tranche:
- `mount <dev> ufs2 <path>` and `mount <dev> ffs <path>` through `sys_mount` dispatch.
- Root inode discovery and pathname walk under the mounted tree.
- Directory entry decode into auxv6 `struct dirent` iteration.
- Regular-file reads via direct + single-indirect pointers.
- Symlink target reads.

Not supported in this tranche:
- Any write operation.
- Full UFS feature matrix (soft updates, snapshots, journaling/suj semantics).
- Broader geometry/endianness variant handling.
- Multi-level indirect reads beyond the first level.

## Integration Points

Files touched for backend integration:
- `kernel/fs/vfs_ufs2.c`: UFS2 superblock/inode/dir parsing + vnode/vfs ops.
- `kernel/core/sysfile.c`: `ufs2`/`ffs` mount-type dispatch.
- `include/defs.h`, `include/vfs.h`: `vfs_ufs2_init()` declaration.
- `Makefile`: adds `kernel/fs/vfs_ufs2.o`.

The backend follows the same VFS contract used by ext2/msdosfs/isofs/btrfs:
- Mount allocates per-filesystem state and stores it in `mount.fs_data`.
- `root_inode`, `namei`, and `nameiparent` are handled by backend ops.
- Unsupported mutating operations fail with deterministic read-only errors.

## On-Disk Assumptions (Current)

Mount-time probe currently assumes:
- UFS2 superblock is at byte offset 65536.
- UFS2 magic matches at the expected FreeBSD/OpenBSD-style field offset.
- Geometry fields (`bsize`, `fsize`, `ipg`, `fpg`, `inopb`, `iblkno`) are sane and self-consistent.

These assumptions are intentionally conservative for tranche 1 and may reject valid-but-unhandled images.

## Known Limitations

- Double/triple-indirect block traversal is not implemented yet.
- Mount parser currently uses a conservative set of superblock offsets.
- Directory iteration prioritizes correctness and simplicity over speed.

## Missing Work Checklist

Read-only correctness and compatibility:
- [ ] Add double-indirect and triple-indirect regular-file read support.
- [ ] Validate/expand superblock field parsing for wider BSD UFS2 image sets.
- [ ] Harden directory and inode bounds checks for malformed media.
- [ ] Add optional compatibility handling for variant UFS2 layouts where practical.

Read-only performance and observability:
- [ ] Add lightweight counters for mount parse failures and directory parse failures.
- [ ] Add optional procfs/debug visibility for UFS2 mount geometry.
- [ ] Improve directory read path efficiency for large directories.

Write-path tranche (future, out of scope now):
- [ ] Define safe metadata update strategy.
- [ ] Implement creation/removal/rename/truncate flows.
- [ ] Add crash-safety and rollback behavior validation.

## Suggested Validation

Inside auxv6 guest:
1. Mount a known image: `mount /dev/<node> ufs2 /mnt`.
2. Verify alias path: `umount /mnt && mount /dev/<node> ffs /mnt`.
3. List root and subdirs: `ls -la /mnt`.
4. Read regular files and symlinks: `cat /mnt/<file>` and symlink targets.
5. Confirm write rejection: `echo x > /mnt/newfile` should fail.

## Host Tooling Status

Current status is intentionally documented to match the btrfs writeup style:

- There is no in-tree host staging script yet (no `tools/stage-ufs2-root.sh`).
- There is no dedicated make target yet (for example, no `make nvme-ufs2.img`).
- On this macOS host, there is no guaranteed default UFS2 image-creation toolchain.

Practical implication:

- UFS2 backend development can continue in-tree, but repeatable guest validation currently depends on bringing a prebuilt UFS2 image from another environment.

Recommended image-build environments until auxv6 ships tooling:

- FreeBSD/OpenBSD/NetBSD host using native FFS/UFS tools.
- Linux host only if a verified UFS2-capable toolchain is available and documented for the specific distro.

Planned follow-on (not in this tranche):

- Add host tooling equivalent to the btrfs workflow:
	- `tools/stage-ufs2-root.sh`
	- `make nvme-ufs2.img`
	- `make qemu-nvme-ufs2` / `make qemu-nox-nvme-ufs2`
