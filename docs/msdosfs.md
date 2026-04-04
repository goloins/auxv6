# msdosfs — FAT Filesystem Driver

## Overview

The `msdosfs` driver (implemented in `kernel/fs/vfs_msdosfs.c`) provides
read/write access to FAT12, FAT16, and FAT32 volumes via the auxv6 VFS
interface. FAT type is determined at mount time by cluster count per the
Microsoft FAT specification. As of the 2026-04 FAT32 bringup, the backend has
verified create/read/write/truncate/unlink and mkdir/rmdir coverage on an NVMe
FAT32 image, including VFAT long-filename round trips.

## Path/Name Limit Alignment

The original xv6-derived directory contract used a 14-byte `DIRSIZ`, which was
large enough for legacy xv6fs names but too small for VFAT long filenames. The
FAT32 regression work exposed the real failure mode: long-name create succeeded
but subsequent lookup truncated the final component before it reached the VFS
backend, so reopen-by-full-name failed.

The fix was applied at the root contract instead of only in `msdos_walk()`:

- `DIRSIZ` now tracks `NAME_MAX + 1`, so kernel component buffers carry the full
  filename plus a trailing NUL.
- The shell now consumes the shared `PATH_MAX` instead of redefining a smaller
  private path limit.
- The kernel/user `getdents()`/`readdir()` dirent bridge was widened to carry
  the larger fixed-size dirent records consistently.

This removed the 14-character truncation at the VFS boundary and allowed FAT32
LFN lookup to work end to end.

## FAT Type Detection

| Cluster count     | FAT type |
|-------------------|----------|
| < 4085            | FAT12    |
| 4085 – 65524      | FAT16    |
| ≥ 65525           | FAT32    |

The driver reads the BPB (BIOS Parameter Block) from sector 0 to compute
the total cluster count and selects the appropriate FAT type.

## Sector Size Constraint

auxv6 uses a fixed kernel block size (`BSIZE = 512`). The BPB bytes-per-sector
field must equal 512; otherwise mount is refused. All sector reads/writes go
through the block cache using `bread`/`bwrite`.

## Inode Number Encoding

Each directory entry is assigned a synthetic inode number at scan time. For
cluster-chain directories (FAT16 data dirs and all FAT32 dirs) the encoding is:

```
inum = cluster << MSDOS_CINUM_SLOT_BITS | slot_in_cluster
     (MSDOS_CINUM_SLOT_BITS = 11)
```

The 11-bit slot field accommodates up to 2048 directory entries per cluster —
more than any realistic cluster size (512-byte cluster / 32-byte dirent = 16
slots; 4096-byte cluster = 128 slots). The remaining 21 bits hold the cluster
number, sufficient for the FAT32 maximum of ~268 million clusters.

FAT16 root directory entries use a separate scheme:

```
inum = MSDOS_ROOT16_INUM_BASE | slot     (MSDOS_ROOT16_INUM_BASE = 0x80000000)
```

The high bit is clear for all valid data-cluster inums (FAT32 limits clusters
to 28 bits), so the two spaces never collide.

## Long Filename (LFN) Support

VFAT long filenames are supported for both read and create.

### Reading LFN Entries

The directory scanner (`msdos_dir_scan_fat16_root` and
`msdos_dir_scan_cluster_chain`) accumulates LFN segments using a
`fat_lfn_state` buffer. When the 8.3 entry that terminates a sequence is
encountered, the assembled name is passed to the visitor callback alongside
the short-name dir entry. The visitor uses the LFN when present in preference
to the 8.3 name for `dirlookup` and `msdos_read`.

### Writing LFN Entries

`msdos_create` generates the required number of LFN directory entries
automatically when the requested name does not fit cleanly as an 8.3 name:

1. `msdos_generate_shortname` converts the long name to an 8.3 short name
   using the `~1` numeric-tail convention.
2. `msdos_lfn_checksum` computes the checksum linking the LFN segments to
   the 8.3 entry.
3. `msdos_write_lfn_entries` writes the segments in reverse sequence order
   (highest-sequence entry first) immediately before the 8.3 entry.

`msdos_find_free_run` locates a contiguous run of `N+1` free (or deleted)
directory slots to hold the LFN segments plus the 8.3 entry together.

Names that are already legal 8.3 — all uppercase, no spaces, ≤8-char base,
≤3-char extension, no LFN-forbidden characters — are written as bare 8.3
entries with no LFN prefix.

## Directory Operations

### lookup (dirlookup)
Scans the directory chain passing `name` to `msdos_lookup_visit`. The visitor
first tries a case-insensitive 8.3 comparison (`msdos_component_to_83`
uppercases the name before comparing). If that fails and an LFN was
accumulated, it falls back to a case-insensitive ASCII LFN comparison.

### create (T_FILE)
1. Convert name; choose LFN or bare 8.3 path.
2. Find a free run of `n_segs + 1` contiguous slots.
3. Allocate the first free cluster for the new file.
4. Write LFN segments (if any) then the 8.3 dir entry.

### create (T_DIR / mkdir)
Same as T_FILE but:
1. Allocates a cluster for the directory data.
2. Writes `.` and `..` dot entries into the first sector of the new cluster.
3. Sets the `ATTR_DIRECTORY` attribute in the 8.3 dir entry.

### remove (unlink / rmdir)
`msdos_remove` marks the 8.3 dir entry deleted (`name[0] = 0xE5`) and frees
the cluster chain. Before removing a directory it scans the cluster chain for
any non-dot entries; if any are found it returns `-ENOTEMPTY` (mapped from
`-1`).  LFN prefix entries preceding the target 8.3 entry are marked deleted
in the same operation by scanning backward from the 8.3 slot and checking the
checksum.

## FSInfo Sector (FAT32)

FAT32 volumes carry a FSInfo sector (sector index given by `BPB_FSInfo`). The
driver reads FSInfo at mount and caches `free_count` and `nxt_free` into
`msdos_mount_data`. `msdos_update_fsinfo` writes the sector back after every
cluster allocation or free. If the FSInfo signatures are invalid the free count
is set to the sentinel `0xFFFFFFFF` (meaning "unknown") and reads are skipped.

## VFS Capabilities

The driver registers with the following VFS capability flags:

```c
VFS_CAP_DIRLOOKUP | VFS_CAP_READDIR | VFS_CAP_CREATE
| VFS_CAP_REMOVE  | VFS_CAP_MKDIR
```

`VFS_CAP_MKDIR` was added as part of the FAT32 bringup; earlier versions only
supported `T_FILE` creation.

## Test Infrastructure

| File | Purpose |
|------|---------|
| `user/fatregress.c` | Mountpoint-driven kernel-user regression test for FAT16 and FAT32 |
| `tools/stage-fat32-root.sh` | Builds a 128 MB seeded FAT32 image (`nvme-fat32.img`) |
| `Makefile` targets: `nvme-fat32.img`, `fat32-reset`, `qemu-nvme-fat32`, `qemu-nox-nvme-fat32` | Build and run FAT32 QEMU sessions |

The FAT32 test image is attached as the second NVMe device. `fatregress`
now accepts `[-d] [mountpoint]` and no longer hardcodes device mounts. It
gracefully skips seeded-read checks if the image was built without `mtools`
(the image is still formatted and writable, but the seed content is absent).

## Verified Manual Validation

The current FAT32 path was validated manually in guest with:

```sh
mkdir /mnt/fat32
mount /dev/nda msdosfs /mnt/fat32
fatregress -d /mnt/fat32
```

Observed result:

- FAT32 mount on `/dev/nda` succeeded.
- Short-name create/read/unlink passed.
- Multi-block growth/truncate passed.
- Long-filename create/read/unlink passed.
- Directory create/file-in-dir/remove passed.
- Seeded reads skipped cleanly when the image lacked mtools-populated content.

## Recent Hardening

Post-bringup cleanup landed in the backend after the initial FAT32 validation:

- FAT directory entries now receive real FAT create/write/access timestamps
  derived from the RTC instead of zero-filled metadata.
- `stat(2)` on msdosfs files now reports decoded FAT access/modify/create times
  instead of returning all-zero time fields.
- Unlinking a VFAT long filename now deletes the preceding LFN slots as well as
  the terminal 8.3 entry, preventing stale long-name prefixes from being left
  behind in the directory.
- Create now rejects duplicate generated 8.3 aliases when two long names would
  collide on the same short-name form.
- Rename is now wired through the VFS rename hook with driver-local FAT move
  semantics for same-filesystem file/directory moves, non-directory overwrite,
  cross-directory file moves, and directory parent updates via `..` rewrite.

## Known Limitations

- **Feature parity**: The backend now supports read/write/create/remove/mkdir/
  truncate/rename semantics, but not hard links, symlinks, chmod/chown, or
  richer metadata operations.
- **Cluster size > 512 B**: The `msdos_find_free_run` scan allocates new cluster
  pages for the directory when slots are exhausted. This works but does not
  compact previously-deleted slots across cluster boundaries.
- **Timestamps**: Directory entry `crtDate`, `wrtDate`, etc. are written as 0
  are now populated, but FAT still cannot represent full Unix timestamp or
  timezone semantics.
- **Encoding**: LFN segment assembly handles only the subset of UTF-16LE that
  maps 1:1 to 7-bit ASCII. Non-ASCII characters in LFN entries are replaced
  with `?` during read.
- **Seed image population**: `tools/stage-fat32-root.sh` relies on host `mtools`
  for seeded test content. Without it, FAT32 regression still validates runtime
  create/read/write/remove paths, but seeded-read checks skip by design.
- **FAT12**: Mount detection is implemented but FAT12 I/O has not been tested
  and the cluster-link read/write functions do not handle the 12-bit packed
  entries correctly. FAT12 volumes should not be used.
