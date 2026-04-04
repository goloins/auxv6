# fatregress(1)

## Name
fatregress - FAT filesystem regression test suite.

## Synopsis
```
fatregress [-d] [mountpoint]
```

## Duty
Run a regression test pass against the FAT filesystem driver (msdosfs) at the
given mountpoint. The test is mountpoint-driven and does not perform mounts or
umounts on its own.

## Tests Performed

### Generic tests (against the requested mountpoint)
1. **Seeded reads** — Verifies `hello.txt` and `subdir/note.txt` against known content.
2. **Small write/read/unlink** — Creates a small file, reads back, unlinks.
3. **Growth and truncate** — Writes a 9 KB pattern across multiple sectors,
   reads back, verifies byte-for-byte, then truncates and confirms zero size.
4. **Seeded short-name reads** — Verifies `hello.txt` and `subdir/note.txt` (8.3 names).
5. **LFN reads** — Verifies `longfilename.txt`, `longnamedir/readme.txt`, and
   `another-long-name-file.txt` (long filename entries).
6. **LFN write/unlink** — Creates a file with a name longer than 8.3, reads it
   back, then unlinks it.
7. **Rename checks** — Exercises same-directory overwrite rename,
   cross-directory file rename, and directory rename with subtree rejection.
8. **mkdir / file in dir / rmdir** — Creates `/fat32/newdir`, writes a file inside,
   reads back, unlinks the file, then removes the empty directory.

## Options
- `-d`: Enable verbose debug logging.
- `mountpoint`: Filesystem mountpoint to test. Defaults to `/fat`.

## Notes
The FAT32 test image is built by `tools/stage-fat32-root.sh` into `nvme-fat32.img`
and attached as the second NVMe device in the QEMU targets `qemu-nvme-fat32`
and `qemu-nox-nvme-fat32`.

If the image was built without host `mtools`, seeded files may be absent and
the seeded-read checks will skip while the create/read/write/remove checks still run.

## Source Audit
- Source file: user/fatregress.c
- Last updated: 2026-04-04
