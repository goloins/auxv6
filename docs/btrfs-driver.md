# Btrfs Driver (Read-Only, Initial Tranche)

## Progress Snapshot (2026-04)

Current completion estimate: ~35% of the long-term Btrfs plan.

Landed now:
- Read-only VFS backend is in-tree and mountable via `btrfs` filesystem type.
- Root/chunk tree parsing for single-device images is functional.
- Directory traversal and file/symlink reads work for the supported extent forms.
- Linux-host image tooling is wired for deterministic NVMe test-image bring-up.

Still missing for a broader read-only milestone:
- Coverage for compressed extent formats.
- Broader metadata validation/hardening under malformed on-disk data.
- Better lookup/search performance than linear/bounded scans.
- Broader subvolume and root-selection semantics beyond current defaults.

## Scope

auxv6 now ships an initial read-only Btrfs backend wired through the VFS layer.

Supported in this tranche:
- `mount <dev> btrfs <path>` through `sys_mount` filesystem dispatch
- Single-device filesystems (`num_devices == 1`)
- Metadata-tree traversal (root tree, chunk tree, mounted fs tree)
- Directory lookup and `readdir` via directory-index items
- Regular-file read path for uncompressed extents
- Symlink read path via regular data-extents/inline extents

Not supported in this tranche:
- Any write path (create/link/unlink/rename/truncate/setattr)
- Multi-device profiles, RAID mapping, scrub/balance features
- Compressed or encrypted file extents
- Subvolume snapshot management operations
- Full B-tree keyed search acceleration (current implementation does bounded scans)

## Integration Points

Files touched for backend integration:
- `kernel/fs/vfs_btrfs.c`: Btrfs on-disk parsing + vnode/vfs ops
- `kernel/core/sysfile.c`: `btrfs` mount-type dispatch
- `include/defs.h`, `include/vfs.h`: `vfs_btrfs_init()` declaration
- `Makefile`: adds `kernel/fs/vfs_btrfs.o`

The backend keeps the VFS contract used by ext2/msdosfs/isofs:
- Mount allocates per-filesystem state and stores it in `mount.fs_data`
- `root_inode`, `namei`, and `nameiparent` operate entirely through vnode interfaces
- Unsupported mutating operations return `-1` consistently for read-only behavior

## On-Disk Constraints Enforced

Mount currently rejects volumes unless all are true:
- Superblock magic matches Btrfs
- `num_devices == 1`
- `nodesize == sectorsize`
- `nodesize >= 4096` and `nodesize <= PGSIZE`
- Root and chunk-root pointers are present

These constraints keep parsing and memory usage compatible with current kernel assumptions.

## Directory and Inode Behavior

- `.` is handled locally for directory lookup.
- `..` is resolved using inode-reference metadata when present.
- Directory enumeration emits synthetic `.` and `..` first, then Btrfs entries.
- Inode metadata (`type`, `mode`, `uid`, `gid`, `nlink`, `size`) is projected into auxv6 inode/stat fields.

## Known Limitations

- Read path only supports uncompressed extents (`compression == 0`).
- Inline extent handling is minimal and intended for read-only portability.
- Directory lookup currently prefers correctness over asymptotic performance.

## Missing Work Checklist

Read-only correctness and compatibility:
- [ ] Add compressed-extent decode support (at least the common case used by modern images).
- [ ] Harden tree/item bounds validation and fail-closed behavior on malformed leaves.
- [ ] Improve parent/inode-reference resolution edge cases for complex directory topologies.
- [ ] Expand coverage for unusual but legal inode/extent layouts.

Read-only performance and observability:
- [ ] Replace broad tree scans with key-directed search for lookup/read hot paths.
- [ ] Add optional debug counters for tree walks, lookup misses, and extent-read fallbacks.
- [ ] Add procfs or debug telemetry hooks suitable for quick regression triage.

Write-path tranche (future, intentionally out of scope now):
- [ ] Define transactional write model and minimal supported feature set.
- [ ] Implement create/link/unlink/rename/truncate/setattr with proper metadata updates.
- [ ] Add durability and crash-safety validation before enabling write capability flags.

## Suggested Validation

Inside auxv6 guest:
1. Mount a known Btrfs image: `mount /dev/<node> btrfs /mnt`
2. Check root listing: `ls -la /mnt`
3. Read regular files: `cat /mnt/<file>`
4. Verify symlink behavior: `ls -l /mnt` and `cat` through symlink targets
5. Confirm write rejection: `echo x > /mnt/newfile` should fail

Host-side image creation should continue using host Btrfs tooling (`mkfs.btrfs`) and existing image attach workflows (loop/NVMe/virtio) already used in auxv6.

## Linux Host Tooling

Btrfs image generation is now wired into the build on Linux hosts via `tools/stage-btrfs-root.sh`.

Useful targets:
- `make nvme-btrfs.img` builds a deterministic read-only test image with sample files and a symlink.
- `make qemu-nvme-btrfs` boots auxv6 with the Btrfs image attached to NVMe.
- `make qemu-nox-nvme-btrfs` does the same in serial-only mode.
- `make btrfs-reset` rebuilds the image from scratch.

On macOS, these targets fail with a clear error because `mkfs.btrfs` is Linux-only for this workflow.
