#!/bin/sh
# build-auxv6-img.sh <rootdir> <output-img>
#
# Creates a single bootable disk image:
#   sector 0          : GRUB boot.img (MBR code, 446 bytes preserved + partition table)
#   sectors 1-2047    : GRUB core.img (i386-pc, multiboot + ext2 + part_msdos modules)
#   sector 2048+      : ext2 root partition (512 MiB)
#
# The partition receives /boot/grub/grub.cfg with a single multiboot entry that
# passes root=/dev/hda1 init=/sbin/init to the kernel.
#
# Host tool requirements:
#   macOS  : i686-elf-grub  (brew install i686-elf-grub)
#            util-linux      (brew install util-linux)   -- for sfdisk
#            e2fsprogs       (brew install e2fsprogs)    -- for mke2fs
#   Linux  : grub-pc-bin grub-common util-linux e2fsprogs

set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <rootdir> <output-img>" >&2
  exit 1
fi

ROOTDIR="$1"
OUTIMG="$2"

if [ ! -d "$ROOTDIR" ]; then
  echo "error: rootdir '$ROOTDIR' does not exist" >&2
  exit 1
fi

if [ ! -f "${ROOTDIR}/aux.kern" ]; then
  echo "error: ${ROOTDIR}/aux.kern not found" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Locate GRUB tools
# ---------------------------------------------------------------------------
GRUB_MACOS_BIN="/opt/homebrew/Cellar/i686-elf-grub/2.12/bin"
GRUB_MACOS_LIB="/opt/homebrew/Cellar/i686-elf-grub/2.12/lib/i686-elf/grub/i386-pc"

GRUB_MKIMAGE=""
GRUB_LIB_DIR=""

if [ -x "${GRUB_MACOS_BIN}/i686-elf-grub-mkimage" ]; then
  GRUB_MKIMAGE="${GRUB_MACOS_BIN}/i686-elf-grub-mkimage"
  GRUB_LIB_DIR="$GRUB_MACOS_LIB"
elif command -v i686-elf-grub-mkimage >/dev/null 2>&1; then
  GRUB_MKIMAGE="i686-elf-grub-mkimage"
  GRUB_LIB_DIR="$GRUB_MACOS_LIB"
elif command -v grub-mkimage >/dev/null 2>&1; then
  GRUB_MKIMAGE="grub-mkimage"
  # Search common Linux paths for i386-pc modules
  for d in /usr/lib/grub/i386-pc /usr/share/grub /usr/lib/grub-pc /usr/lib/grub/i386-pc-signed; do
    if [ -d "$d" ] && ls "$d"/*.mod >/dev/null 2>&1; then
      GRUB_LIB_DIR="$d"
      break
    fi
  done
fi

if [ -z "$GRUB_MKIMAGE" ]; then
  echo "error: grub-mkimage not found." >&2
  echo "  macOS : brew install i686-elf-grub" >&2
  echo "  Linux : sudo apt install grub-pc-bin grub-common" >&2
  exit 1
fi

if [ -z "$GRUB_LIB_DIR" ] || [ ! -f "${GRUB_LIB_DIR}/boot.img" ]; then
  echo "error: GRUB i386-pc module directory not found (expected ${GRUB_LIB_DIR})" >&2
  exit 1
fi

BOOT_IMG="${GRUB_LIB_DIR}/boot.img"

# ---------------------------------------------------------------------------
# Locate sfdisk (partition table writer)
# ---------------------------------------------------------------------------
SFDISK=""
# Homebrew util-linux on macOS installs sfdisk under a keg path to avoid
# shadowing system tools.
for candidate in \
    sfdisk \
    /opt/homebrew/opt/util-linux/sbin/sfdisk \
    /opt/homebrew/sbin/sfdisk \
    /usr/local/sbin/sfdisk; do
  if command -v "$candidate" >/dev/null 2>&1 || [ -x "$candidate" ]; then
    SFDISK="$candidate"
    break
  fi
done

if [ -z "$SFDISK" ]; then
  echo "error: sfdisk not found." >&2
  echo "  macOS : brew install util-linux" >&2
  echo "  Linux : sudo apt install util-linux" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Locate mke2fs
# ---------------------------------------------------------------------------
MKE2FS=""
for candidate in mke2fs /sbin/mke2fs /opt/homebrew/opt/e2fsprogs/sbin/mke2fs; do
  if command -v "$candidate" >/dev/null 2>&1 || [ -x "$candidate" ]; then
    MKE2FS="$candidate"
    break
  fi
done

if [ -z "$MKE2FS" ]; then
  echo "error: mke2fs not found." >&2
  echo "  macOS : brew install e2fsprogs" >&2
  echo "  Linux : sudo apt install e2fsprogs" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Locate ELF patch tools (used to make staged aux.kern GRUB-friendly)
# ---------------------------------------------------------------------------
NM_TOOL=""
OBJCOPY_TOOL=""
for candidate in i386-jos-elf-nm nm; do
  if command -v "$candidate" >/dev/null 2>&1; then
    NM_TOOL="$candidate"
    break
  fi
done
for candidate in i386-jos-elf-objcopy objcopy; do
  if command -v "$candidate" >/dev/null 2>&1; then
    OBJCOPY_TOOL="$candidate"
    break
  fi
done

if [ -z "$NM_TOOL" ] || [ -z "$OBJCOPY_TOOL" ]; then
  echo "error: missing nm/objcopy tool for staged kernel ELF fixup" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------
SECT_SIZE=512
PART_START=2048           # first sector of partition 1 (standard 1 MiB alignment)
PART_SECTS=1048576        # 512 MiB partition  (512 * 1048576 = 536870912 bytes)
IMG_SECTS=1050624         # total image: partition + a small tail buffer

# mke2fs size argument is in filesystem blocks, not sectors.
# We force 1KiB blocks so this count maps exactly to 512 MiB.
FS_BLOCK_SIZE=1024
FS_BLOCKS=524288          # 512 MiB / 1 KiB

# ---------------------------------------------------------------------------
# Inject grub.cfg into rootdir
# ---------------------------------------------------------------------------
mkdir -p "${ROOTDIR}/boot/grub"
cat > "${ROOTDIR}/boot/grub/grub.cfg" << 'GRUBCFG'
set timeout=3
set default=0

menuentry "auxv6" {
    multiboot /aux.kern root=/dev/hda1 init=/sbin/init
    boot
}
GRUBCFG

# GRUB validates ELF entry against PT_LOAD VirtAddr ranges. The normal aux.kern
# image uses a physical e_entry for the legacy bootblock path, which GRUB rejects.
# For the staged single-image root only, rewrite e_entry to virtual 'entry'.
ENTRY_VMA=$("$NM_TOOL" -n "${ROOTDIR}/aux.kern" | awk '/ entry$/{print $1; exit}')
if [ -z "$ENTRY_VMA" ]; then
  echo "error: could not resolve 'entry' symbol from ${ROOTDIR}/aux.kern" >&2
  exit 1
fi
"$OBJCOPY_TOOL" --set-start "0x${ENTRY_VMA}" "${ROOTDIR}/aux.kern"

# ---------------------------------------------------------------------------
# Build ext2 partition image from staged rootdir
# ---------------------------------------------------------------------------
WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

PART_IMG="${WORKDIR}/part.img"
CORE_IMG="${WORKDIR}/core.img"

if command -v fakeroot >/dev/null 2>&1; then
  fakeroot sh -c "chown -R 0:0 '$ROOTDIR' && '$MKE2FS' -q -t ext2 -b $FS_BLOCK_SIZE -d '$ROOTDIR' -F '$PART_IMG' $FS_BLOCKS"
elif sudo -n true 2>/dev/null; then
  sudo "$MKE2FS" -q -t ext2 -b "$FS_BLOCK_SIZE" -d "$ROOTDIR" -F "$PART_IMG" "$FS_BLOCKS"
else
  echo "warning: building ext2 without elevated privileges; file ownership will be wrong" >&2
  "$MKE2FS" -q -t ext2 -b "$FS_BLOCK_SIZE" -d "$ROOTDIR" -F "$PART_IMG" "$FS_BLOCKS"
fi

# ---------------------------------------------------------------------------
# Build GRUB core image (modules baked in, prefix points at partition grub dir)
# ---------------------------------------------------------------------------
"$GRUB_MKIMAGE" \
  -O i386-pc \
  -o "$CORE_IMG" \
  -p '(hd0,msdos1)/boot/grub' \
  --directory "$GRUB_LIB_DIR" \
  biosdisk part_msdos ext2 multiboot normal

# Verify core.img fits in the pre-partition gap (sectors 1 through PART_START-1)
CORE_BYTES=$(wc -c < "$CORE_IMG")
CORE_SECTS=$(( (CORE_BYTES + SECT_SIZE - 1) / SECT_SIZE ))
MAX_GAP=$(( PART_START - 1 ))
if [ "$CORE_SECTS" -gt "$MAX_GAP" ]; then
  echo "error: core.img is $CORE_SECTS sectors but gap is only $MAX_GAP sectors" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Assemble final disk image
# ---------------------------------------------------------------------------

# 1. Zero image
dd if=/dev/zero of="$OUTIMG" bs=512 count=$IMG_SECTS 2>/dev/null

# 2. Write MBR partition table (one primary ext2 partition, active, from PART_START)
printf '%s\n' "${PART_START},,83,*" | "$SFDISK" -q "$OUTIMG"

# 3. Overlay GRUB bootloader code (first 446 bytes of boot.img), preserving
#    the partition table that sfdisk just wrote at bytes 446-511.
dd if="$BOOT_IMG" of="$OUTIMG" bs=1 count=446 conv=notrunc 2>/dev/null

# 4. Write core.img starting at sector 1 (boot.img's default core location)
dd if="$CORE_IMG" of="$OUTIMG" bs=512 seek=1 conv=notrunc 2>/dev/null

# 5. Write ext2 partition at sector PART_START
dd if="$PART_IMG" of="$OUTIMG" bs=512 seek=$PART_START conv=notrunc 2>/dev/null

echo "$(basename "$0"): built $OUTIMG ($(( IMG_SECTS * SECT_SIZE / 1048576 )) MiB, ext2 root at sector ${PART_START})"
