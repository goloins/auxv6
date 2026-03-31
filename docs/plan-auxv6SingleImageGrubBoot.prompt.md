# Plan: True single-image GRUB-booted auxv6

**TL;DR**: Replace the two-image QEMU setup (xv6.img bootblock + test_ext2.img rootfs) with a
single `auxv6.img`: a 130 MB raw disk with an MBR partition table, one ext2 partition holding
the rootfs + `/aux.kern`, and GRUB2 (via `grub-mkstandalone` — no host `grub-install` needed)
embedded in the MBR gap. The kernel already has a multiboot1 header in `kernel/core/entry.S`;
we extend it to save `EBX` before paging, parse the GRUB-supplied cmdline (`root=`, `init=`),
and fall back to `mbi->boot_device` then a block scan. `xv6.img` is demoted to legacy-only.

---

## Decisions

- **Root detection**: cmdline `root=` wins; fall back to multiboot `boot_device`; fall back to
  auto-scan first ext2-valid block device.
- **Kernel location in rootfs**: `/aux.kern`
- **GRUB tooling**: `grub-mkstandalone` — bakes all modules into `core.img`; requires only
  `grub-pc-bin` + `grub-common` on host, no `grub-install` or on-disk module tree.
- **xv6.img fate**: kept as legacy-only target for xv6fs regression; not the default build.

---

## Phase 1 — Image sizing & kernel packaging

Steps are independent and can be done in parallel.

### Step 1 — Increase ext2 image to 128 MB

**File:** `tools/stage-ext2-root.sh`

Change:
```
mke2fs ... 8192
```
to:
```
mke2fs ... 32768
```
(32 768 × 4 096-byte ext2 blocks = 128 MiB)

### Step 2 — Copy kernel into rootfs staging

**File:** `tools/stage-ext2-root.sh`

Accept a `kernelfile` positional arg; after setting up the staging directory:
```bash
cp "$kernelfile" "$rootdir/aux.kern"
```

**File:** `Makefile`

Add `aux.kern` as a dependency of `test_ext2.img` (and the future `auxv6.img` target); pass
it as an argument to the stage script.

---

## Phase 2 — Multiboot info passthrough in kernel

Depends on Phase 1 for end-to-end testing, but can be developed independently.

### Step 3 — Add `include/multiboot.h`

Define the multiboot1 `struct multiboot_info` per the GNU Multiboot specification:
- Fields: `flags`, `mem_lower`, `mem_upper`, `boot_device`, `cmdline` (physical address),
  `mods_count`, `mods_addr`, `mmap_length`, `mmap_addr`
- `#define MULTIBOOT_MAGIC 0x2BADB002`

### Step 4 — Save EAX/EBX in `kernel/core/entry.S` before paging

Right after the `entry:` label, before any CR4/CR3/CR0 modifications:
```asm
movl %eax, (V2P_WO(mboot_magic))
movl %ebx, (V2P_WO(mboot_info_phys))
```

Declare `uint mboot_magic` and `uint mboot_info_phys` as globals in `kernel/core/multiboot.c`.

### Step 5 — Add `kernel/core/multiboot.c` — cmdline parser & root resolver

`multiboot_init()` — called early in `main()` after basic memory setup:

1. Validate `mboot_magic == MULTIBOOT_MAGIC`; if not, leave `boot_rootdev = 0` (fallback path)
2. `mbi = P2V(mboot_info_phys)`
3. If `mbi->flags & (1<<2)` (cmdline present):
   - Parse `P2V(mbi->cmdline)` for `root=/dev/hdXN` → `DISK_PART_DEV(X, N)` (partition) or
     `DISK_DEV(X)` (whole disk if no partition suffix)
   - Parse `init=/path` → copy into global `char boot_init_path[64]`
4. Else fall back to `mbi->boot_device`:
   - Byte 3 (0x80 = hda, 0x81 = hdb, …) → disk index
   - Byte 2 (0xFF = whole disk, else partition number) → `DISK_PART_DEV` or `DISK_DEV`
5. Store resolved device number in `uint boot_rootdev`

### Step 6 — Runtime root device & init path

**File:** `kernel/fs/vfs.c`

Replace `static uint vfs_rootdev = ROOTFS_DEV` with `extern uint boot_rootdev`. In
`vfs_init()`, prefer `boot_rootdev` when non-zero; fall back to compile-time `ROOTFS_DEV`.

**File:** `kernel/core/proc.c`

In `kinit_exec()`, prepend `boot_init_path` (if non-empty) before the hardcoded search list
`{"/sbin/init", "/bin/init", "/init", 0}`.

### Step 7 — Auto-scan fallback (last resort)

If `boot_rootdev` is still 0 after `multiboot_init()` (e.g., booted without GRUB), scan
`DISK_DEV(0)`, `DISK_DEV(1)`, … testing each for a valid ext2 superblock signature (`0xEF53`
at byte 1080). Implemented in `multiboot.c::multiboot_scan_for_rootdev()`; called from
`vfs_init()` as the final fallback.

---

## Phase 3 — Partition block device support

May run in parallel with Phase 2. This is the highest-risk scope item.

### Step 8 — Verify / implement `DISK_PART_DEV` LBA offset translation

1. Read `include/blockdev.h` and the IDE driver to determine whether `DISK_PART_DEV(disk,
   part)` already offsets reads/writes by the partition's `start_lba`.
2. If not implemented: during `ideinit()`, read sector 0 of each disk, validate the `0xAA55`
   MBR signature, parse the four primary partition entries, and register
   `(start_lba, size_lba)` per slot. `DISK_PART_DEV` I/O then adds `start_lba` to every
   sector address.
3. **Temporary bridge**: while partition support is being built, `boot_rootdev` can resolve to
   `DISK_DEV(0)` (whole-disk). A separate `auxv6-nopart.img` target (no partition table,
   raw ext2 from byte 0) lets Phases 1–2 and 4 be validated end-to-end immediately.

---

## Phase 4 — Single `auxv6.img` with GRUB

Depends on Phases 1 and 3.

### Step 9 — Write `tools/build-auxv6-img.sh`

1. Check host deps: `grub-mkstandalone`, `sfdisk`, `mke2fs`; print install hint on failure:
   `apt install grub-pc-bin grub-common`
2. Detect GRUB i386-pc module directory: `/usr/lib/grub/i386-pc` or `/usr/share/grub/i386-pc`
3. Write `<tmpdir>/grub.cfg`:

```
set timeout=5
set default=0

menuentry "auxv6" {
  insmod ext2
  insmod part_msdos
  insmod multiboot
  set root='(hd0,msdos1)'
  multiboot /aux.kern root=/dev/hda1 init=/sbin/init
  boot
}

menuentry "auxv6 (legacy init)" {
  insmod ext2
  insmod part_msdos
  insmod multiboot
  set root='(hd0,msdos1)'
  multiboot /aux.kern root=/dev/hda1 init=/sbin/6init
  boot
}
```

4. Copy `grub.cfg` into staged rootdir at `boot/grub/grub.cfg`
5. Build 128 MB ext2 partition image from staged rootdir:
   `mke2fs -q -t ext2 -d <rootdir> -F <partimg> 32768`
6. Create 130 MB disk image: `dd if=/dev/zero of=<output> bs=1M count=130`
7. Partition with `sfdisk`: `label: dos`, partition 1 — start=2048, type=83 (Linux)
8. Write ext2 partition image into disk at sector 2048:
   `dd if=<partimg> of=<output> seek=2048 bs=512 conv=notrunc`
9. Build self-contained GRUB core.img:
   ```
   grub-mkstandalone -O i386-pc -o core.img \
     --modules="biosdisk part_msdos ext2 multiboot normal" \
     /boot/grub/grub.cfg=<tmpdir>/grub.cfg
   ```
10. Embed GRUB into the disk image:
    ```
    dd if=<grub_dir>/boot.img of=<output> bs=446 count=1 conv=notrunc
    dd if=core.img of=<output> seek=1 bs=512 conv=notrunc
    ```
    (Only 446 bytes of boot.img, preserving the MBR partition table at offset 446–511)

### Step 10 — Makefile `auxv6.img` target

```makefile
auxv6.img: aux.kern tools/build-auxv6-img.sh <rootdir-stamp>
	sh tools/build-auxv6-img.sh .ext2root aux.kern auxv6.img
```

Add `auxv6.img` to `.gitignore` and the `clean` target.

### Step 11 — Makefile `qemu-auxv6` target (new default)

```makefile
qemu-auxv6: auxv6.img
	$(QEMU) -serial mon:stdio \
	  -drive file=auxv6.img,index=0,media=disk,format=raw \
	  $(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)
```

---

## Phase 5 — Legacy preservation

### Step 12 — Demote xv6.img

- Keep `xv6.img` build rule in Makefile as-is.
- `qemu-xv6root` and all related `*-xv6root` targets: unchanged.
- Remove `xv6.img` from any top-level default dependency chains.
- Add a comment in Makefile noting that xv6.img is for regression testing only.

---

## Files modified / created

| File | Change |
|------|--------|
| `tools/stage-ext2-root.sh` | +128 MB image size, +copy kernel to `/aux.kern` |
| `tools/build-auxv6-img.sh` | **NEW** — full disk image builder with GRUB |
| `kernel/core/entry.S` | Save EAX/EBX (multiboot regs) before paging setup |
| `include/multiboot.h` | **NEW** — multiboot1 struct definitions |
| `kernel/core/multiboot.c` | **NEW** — cmdline parser, boot_device decode, ext2 scan |
| `kernel/fs/vfs.c` | Use runtime `boot_rootdev` instead of compile-time `ROOTFS_DEV` |
| `kernel/core/proc.c` | Prepend `boot_init_path` to init search list |
| `kernel/driver/ide.c` | MBR partition table parsing + LBA offset for `DISK_PART_DEV` |
| `Makefile` | `auxv6.img` + `qemu-auxv6` targets; `aux.kern` dep on stage script |

---

## Verification

1. `make aux.kern && make auxv6.img` — image builds without errors
2. `make qemu-auxv6` — QEMU boots; GRUB menu appears, counts down, selects first entry
3. Kernel log shows: multiboot magic recognised, `root=/dev/hda1` parsed
4. Kernel mounts ext2 from hda1, finds `/sbin/init`, reaches shell prompt
5. `ls /aux.kern` inside the OS — kernel file present on the rootfs
6. Select "auxv6 (legacy init)" entry — boots with `/sbin/6init` instead
7. Remove `root=` from grub.cfg, rebuild — `boot_device` fallback still boots correctly
8. `make qemu-xv6root` — still boots xv6fs, no regression

---

## Further Considerations

1. **Host dependency**: `grub-mkstandalone` needs `grub-pc-bin` + `grub-common`.
   Ubuntu/Debian: `apt install grub-pc-bin grub-common`.
   The build script must detect absence and print this one-liner before exiting non-zero.

2. **Partition plumbing scope**: Step 8 (partition LBA offsetting in the block layer) is the
   highest-risk item. If it requires significant kernel work, ship the temporary whole-disk
   bridge first so the rest of the pipeline (GRUB boot, multiboot parsing, `/aux.kern` in
   rootfs) can be verified independently.

3. **grub-mkstandalone vs grub-install**: `grub-mkstandalone` bakes every required GRUB module
   directly into `core.img` — no `/boot/grub/i386-pc/*.mod` files are needed on the image at
   runtime. This produces a fully self-contained build artifact and is the correct choice for a
   custom OS image. Standard `grub-install` is designed for host OS installs and is explicitly
   not used here.
