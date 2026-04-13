# Roadmap: Single-image GRUB boot to ext2-root auxv6

## Goal

Ship one boot artifact, `auxv6.img`, that boots directly through GRUB into auxv6 and mounts
ext2 root from its first MBR partition. Retire xv6fs boot paths and retire old-init compatibility
paths. ext2 is the only supported root boot path for this flow.

## Target State

- One disk image: `auxv6.img`.
- MBR partition table with partition 1 as ext2 root.
- Root filesystem partition size: 512 MiB.
- Kernel file in rootfs: `/aux.kern`.
- GRUB loads kernel via multiboot and passes `root=/dev/hda1 init=/sbin/init`.
- Kernel resolves root device at runtime from multiboot cmdline/device data, then ext2 scan fallback.
- Make default run target boots this single image.
- No xv6fs boot targets remain in normal developer workflow.

---

## Current Baseline (already true)

- ext2-root is the default development boot path.
- Kernel is already staged into targetfs as `targetfs/aux.kern` during `aux.kern` build.
- IDE partition table parsing and partition-device offset translation are already implemented.

---

## Phase A - Rootfs and build graph hardening (low risk)

### A1. Increase staged ext2 root size to 512 MiB

**File:** `tools/stage-ext2-root.sh`

- Set ext2 image block count to produce a 512 MiB image.
- Keep current ownership behavior (`fakeroot`/`sudo` fallback) unchanged.

### A2. Make kernel freshness explicit for ext2 image builds

**File:** `Makefile`

- Add `aux.kern` as an explicit prerequisite for `test_ext2.img` and related root image targets.
- Keep targetfs-based kernel staging model; do not add parallel kernel-copy logic in stage script.

### A3. Remove old-init image flow from rootfs plan

**File:** `Makefile`

- Remove `test_ext2_oldinit.img` and `qemu-oldinit` targets.
- Remove `UPROGS_OLDINIT` path and old-init staging assumptions.

Definition of done:

1. `make test_ext2.img` reliably rebuilds with current kernel in rootfs.
2. Generated ext2 image size is 512 MiB.
3. Old-init image target family is gone.

---

## Phase B - Multiboot runtime root/init plumbing (core feature)

### B1. Add multiboot interfaces

**Files:** `include/multiboot.h`, `kernel/core/multiboot.c`

- Add multiboot1 structure definitions and `MULTIBOOT_MAGIC`.
- Add globals:
  - `uint mboot_magic`
  - `uint mboot_info_phys`
  - `uint boot_rootdev`
  - `char boot_init_path[64]`

### B2. Capture GRUB registers before paging enable

**File:** `kernel/core/entry.S`

- Save EAX/EBX immediately at `entry:` before CR4/CR3/CR0 changes.

### B3. Parse cmdline and boot_device

**File:** `kernel/core/multiboot.c`

- Parse `root=/dev/hdXN` and map to `DISK_DEV`/`DISK_PART_DEV`.
- Parse `init=/path` into `boot_init_path`.
- If cmdline root missing, decode `boot_device` from multiboot info.
- Keep behavior strict and deterministic; invalid values fall through to fallback.

### B4. Hook runtime root selection into VFS init

**File:** `kernel/fs/vfs.c`

- Prefer `boot_rootdev` when non-zero.
- Fall back to compile-time `ROOTFS_DEV`.
- If still unresolved or unusable, call ext2 root scan fallback.

### B5. Hook runtime init path into init exec

**File:** `kernel/core/proc.c`

- Prepend `boot_init_path` to init candidate list when non-empty.
- Keep built-in fallback list including `/sbin/init`.

### B6. Implement last-resort ext2 root scan fallback

**File:** `kernel/core/multiboot.c`

- Scan eligible block devices and identify ext2 superblock signature.
- Use first valid result as `boot_rootdev`.

Definition of done:

1. Boot with explicit `root=/dev/hda1` mounts expected ext2 root.
2. Boot without `root=` but valid `boot_device` still mounts correct root.
3. Boot with neither path still reaches root via ext2 scan fallback.

---

## Phase C - Single-image builder and Make integration

### C1. Add image builder script

**File:** `tools/build-auxv6-img.sh` (new)

#### Host GRUB tool resolution

The script must probe for tools in this order:

1. **macOS (Homebrew i686-elf-grub):**  
   Prefix `i686-elf-grub-`, base path `/opt/homebrew/Cellar/i686-elf-grub/2.12/bin/`.  
   Installed tools include `i686-elf-grub-mkstandalone`, `i686-elf-grub-mkimage`, `i686-elf-grub-mkrescue`, etc.  
   Detection: `[ -d /opt/homebrew/Cellar/i686-elf-grub ]` or `command -v i686-elf-grub-mkstandalone`.

2. **Linux (native):**  
   Unprefixed tools: `grub-mkstandalone`, `grub-mkimage`.  
   Install hint when missing: `apt install grub-pc-bin grub-common`.

The script sets `GRUB_MKSTANDALONE` and `GRUB_MKIMAGE` variables at startup via probe logic, then uses those variables throughout. If neither is found, exit with an actionable error message covering both platforms.

#### Image build steps

- Validate resolved tool variables plus `sfdisk`, `mke2fs`, `dd`.
- Build partition ext2 image from staged rootfs at 512 MiB.
- Build full raw image with DOS/MBR label and partition 1 at sector 2048.
- Embed GRUB `boot.img` + standalone `core.img` without host `grub-install`.
- Install generated `grub.cfg` with one entry only:
  - `multiboot /aux.kern root=/dev/hda1 init=/sbin/init`

### C2. Add `auxv6.img` target

**File:** `Makefile`

- Add `auxv6.img` target using the new builder and staged rootfs.
- Add image file to clean artifacts.

### C3. Add and promote `qemu-auxv6`

**File:** `Makefile`

- Add `qemu-auxv6` to boot only `auxv6.img` as drive index 0.
- Promote it to default local dev boot target after smoke tests pass.

Definition of done:

1. `make auxv6.img` builds reproducibly.
2. `make qemu-auxv6` reaches login prompt using a single disk image.
3. `/aux.kern` exists in guest root filesystem.

---

## Phase D - Remove xv6fs boot pathways from active workflow

### D1. Remove xv6fs boot path targets

**File:** `Makefile`

- Remove xv6fs-focused boot targets from normal build/test path.
- Remove boot combinations that depend on split bootblock+xv6fs assumptions.

### D2. Keep ext2 as sole supported root boot path

**Files:** `Makefile`, relevant docs in `docs/`

- Update docs and target naming so ext2-root single-image is the canonical path.
- Remove references that imply xv6fs remains a supported root boot option.

Definition of done:

1. Standard `make qemu`/`make qemu-nox` path uses single-image GRUB flow (or aliases to it).
2. No remaining docs describe xv6fs as an active boot path.

---

## Verification Matrix

1. `make aux.kern && make test_ext2.img`.
2. Confirm `test_ext2.img` is 512 MiB.
3. `make auxv6.img`.
4. `make qemu-auxv6`:
   - GRUB menu appears and boots default entry.
   - Kernel logs show multiboot path accepted.
   - Root mounts from `/dev/hda1`.
   - `/sbin/init` exec succeeds.
5. Rebuild with cmdline `root=` removed from GRUB config and verify `boot_device` path.
6. Corrupt cmdline and `boot_device` inputs intentionally and verify ext2 scan fallback boots.
7. Run existing ext2-root smoke tests from guest (`which`, `lsof`, `file`) after boot.

---

## Risks and Mitigations

1. GRUB module/tool availability differs by host.
   - macOS: `i686-elf-grub` Homebrew formula provides all required tools under
     `/opt/homebrew/Cellar/i686-elf-grub/2.12/bin/` with `i686-elf-grub-` prefix.
     (`i686-elf-grub-mkstandalone`, `i686-elf-grub-mkimage`, etc.)
   - Linux: native `grub-pc-bin` / `grub-common` packages provide unprefixed tools.
   - Mitigation: build script probes both locations and sets tool variables; emits
     platform-specific install hint on failure.
2. Root-device parsing may accept malformed cmdline values.
   - Mitigation: reject invalid forms and log chosen source (cmdline, boot_device, scan).
3. Boot-target migration can break existing automation.
   - Mitigation: keep alias targets briefly, then remove once scripts are updated.

---

## Files Expected To Change

- `tools/stage-ext2-root.sh`
- `tools/build-auxv6-img.sh` (new)
- `include/multiboot.h` (new)
- `kernel/core/entry.S`
- `kernel/core/multiboot.c` (new)
- `kernel/fs/vfs.c`
- `kernel/core/proc.c`
- `Makefile`
- Relevant docs under `docs/` (roadmap/boot documentation updates)

---

## End State Summary

auxv6 boots from a single GRUB-enabled raw image (`auxv6.img`) directly into ext2-root userland,
with runtime root/init selection from multiboot data and robust fallback behavior. xv6fs boot
compatibility paths are removed from active development workflow.
