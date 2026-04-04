# exfat Driver (Read-Only, Initial Tranche)

## Progress Snapshot (2026-04)

Current completion estimate: ~42% of the long-term exfat plan.

Landed now:
- Read-only VFS backend is in-tree and mountable via `exfat` filesystem type.
- Mount-time boot-sector validation is implemented.
- Directory traversal works over exfat file-entry sets (`0x85`, `0xC0`, `0xC1`).
- Case-insensitive lookup and regular-file reads work for FAT-chained and
  contiguous (`NoFatChain`) streams.
- Makefile/QEMU integration is wired for an NVMe-backed exfat validation path.

Still missing for a broader milestone:
- Write path (`create`, `unlink`, `rename`, `truncate`, allocation updates).
- Allocation bitmap and upcase-table validation/hardening.
- Richer metadata projection (`stat` timestamps/attrs).
- Reliable host-side seeded-image tooling on this macOS workflow.

## Scope

auxv6 now ships an initial read-only exfat backend through the VFS layer.

Supported in this tranche:
- `mount <dev> exfat <path>` through `sys_mount` dispatch.
- Root inode discovery and pathname walk under the mounted tree.
- Directory-entry-set decode into auxv6 `struct dirent` iteration.
- Regular-file reads for contiguous and FAT-chained streams.

Not supported in this tranche:
- Any write operation.
- Full Unicode/upcase-table case-folding fidelity.
- Metadata parity beyond basic mode/size projection.
- Allocation bitmap validation beyond minimum mount geometry checks.

## Integration Points

Files touched for backend integration:
- `kernel/fs/vfs_exfat.c`: exfat parser + vnode/vfs ops.
- `kernel/core/sysfile.c`: `exfat` mount-type dispatch.
- `include/vfs.h`: `vfs_exfat_init()` declaration.
- `Makefile`: adds `kernel/fs/vfs_exfat.o`, `nvme-exfat.img`, and QEMU targets.
- `tools/stage-exfat-root.sh`: host-side image formatting helper.

The backend follows the same VFS contract used by msdosfs/isofs/btrfs/ufs2:
- Mount allocates per-filesystem state and stores it in `mount.fs_data`.
- `root_inode`, `namei`, and `nameiparent` are handled by backend ops.
- Unsupported mutating operations fail deterministically for read-only behavior.

## On-Disk Model Used

The parser consumes exfat file entry sets:

1. Primary file entry (`0x85`)
2. Stream extension entry (`0xC0`)
3. One or more filename entries (`0xC1`)

For each completed set, the backend synthesizes a VFS inode using:

- `attrs` from primary entry (`directory`, `readonly`)
- `first_cluster` and `data_length` from stream extension
- `stream_flags` (including `NoFatChain`) from stream extension
- assembled filename (ASCII subset; non-ASCII currently mapped to `?`)

Volume-label entry sets are skipped from directory visibility.

## Geometry and Validation

Mount initialization validates:

- boot signature (`0xAA55`)
- FS name literal (`EXFAT   `)
- `bytes_per_sector_shift` resolves to `BSIZE` (512)
- non-zero FAT/heap offsets and lengths
- valid root directory cluster (`>= 2`)
- bounds against block-device sector count

Given auxv6's fixed block I/O unit (`BSIZE=512`), this tranche currently
accepts only exfat volumes with 512-byte sectors.

## Cluster/Stream Read Path

Regular-file reads use `exfat_stream_read`:

- For contiguous streams (`NoFatChain`), cluster index maps directly:
  `cluster = first_cluster + index`.
- For chained streams, cluster resolution follows FAT entries.
- Reads are clipped to stream length and mapped sector-by-sector through
  `bread_ok` wrappers.

EOF detection follows exfat FAT semantics (`>= 0xFFFFFFF8` treated as end).

## Known Limitations

- No write path yet (read-only tranche by design).
- Filename decoding is ASCII-oriented; full UTF-16/upcase-table behavior is not
  implemented yet.
- exfat metadata timestamps are not surfaced yet (`stat` time fields stay zero).
- Allocation bitmap and upcase-table integrity are not yet validated beyond
  minimum mount geometry checks.
- Parent (`..`) handling uses synthetic ancestry carried in in-memory inode
  context; this is sufficient for normal traversal in this tranche but should be
  hardened with explicit parent metadata handling in a follow-on pass.

## Suggested Validation

Inside auxv6 guest:
1. Mount a known image: `mount /dev/nda exfat /mnt`.
2. Verify root listing: `ls -la /mnt`.
3. Read regular files when seeded content exists: `cat /mnt/<file>`.
4. Confirm write rejection: `echo x > /mnt/newfile` should fail.

## Build and Run Targets

Makefile additions:

- `nvme-exfat.img` (image build target)
- `exfat-reset` (rebuild helper)
- `qemu-nvme-exfat` (GUI run)
- `qemu-nox-nvme-exfat` (serial run)

Staging helper:

- `tools/stage-exfat-root.sh`

The staging script uses host formatter discovery:

- `mkfs.exfat` (Linux/exfatprogs), or
- `newfs_exfat` (macOS)

and creates a deterministic 128 MB blank exfat volume labeled `AUXV6EXFAT`.

## Host Tooling Status

Current status is intentionally closer to the UFS2/Btrfs writeups than the FAT
workflow:

- `tools/stage-exfat-root.sh` exists and can format a blank exfat image.
- Dedicated make targets now exist (`make nvme-exfat.img`,
  `make qemu-nvme-exfat`, `make qemu-nox-nvme-exfat`).
- On this macOS host, there is not yet a good, repeatable exfat content-staging
  workflow comparable to the FAT32 path.

Practical implication:

- exfat backend development can continue in-tree and the first tranche is not
  abandoned.
- Repeatable guest validation on this macOS workflow is currently limited to
  blank-image mount smoke unless a better host-side exfat population path is
  documented.

Recommended image-build environments until auxv6 ships stronger tooling:

- Linux host with `exfatprogs` and a verified file-population workflow.
- macOS only if a reliable, documented population path is found beyond simple
  formatting with `newfs_exfat`.

Planned follow-on (deferred, not shelved):

- strengthen `tools/stage-exfat-root.sh` from format-only to seeded-image build
- add exfat-focused smoke coverage once seeded content is practical
- continue metadata hardening before enabling any write capability flags

## Mount Usage In Guest

Manual guest commands:

```sh
mkdir /mnt/exfat
mount /dev/nda exfat /mnt/exfat
ls /mnt/exfat
```

(`n0` style numeric aliases also work when mapped to the same block device.)

## Missing Work Checklist

Read-only correctness and compatibility:

- [ ] Parse and validate allocation bitmap entry semantics.
- [ ] Parse and validate upcase-table presence/checksum where practical.
- [ ] Improve Unicode/case-fold behavior beyond the current ASCII-oriented path.
- [ ] Harden malformed entry-set and FAT-chain handling.

Read-only performance and observability:

- [ ] Add optional debug counters for entry-set scans and FAT-chain fallbacks.
- [ ] Add quick diagnostics suitable for mount/read regression triage.

Write-path tranche (future, out of scope now):

- [ ] Define allocation/update model around FAT + allocation bitmap coherence.
- [ ] Implement create/remove/rename/truncate flows.
- [ ] Add durability/correctness validation before enabling write capabilities.

## Next Tranche Plan

1. Metadata hardening:
   - parse/validate allocation bitmap entry
   - parse/validate upcase table entry and checksum where practical
2. Correctness expansion:
   - fuller Unicode case-fold/name matching path
   - richer stat metadata (times/attrs)
3. Mutation tranche:
   - allocation + FAT update primitives
   - create/remove/rename/truncate (file first, then directory)
4. Regression tooling:
   - exfat-focused smoke utility similar in style to `fatregress`
   - scripted mount/read negative tests for malformed entry sets
