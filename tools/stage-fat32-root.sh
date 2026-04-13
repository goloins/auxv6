#!/bin/sh
# stage-fat32-root.sh <staging-dir> <image>
# Create a FAT32 test image using dosfstools mkfs.fat.
# Produces a 128 MB FAT32 volume (large enough that mkfs.fat chooses FAT32
# by the cluster-count rule) with a seeded directory tree and a mix of
# short and long filenames to exercise both the 8.3 and LFN read paths.
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <staging-dir> <image>" >&2
  exit 1
fi

staging_dir="$1"
image="$2"

# Locate mkfs.fat / mkdosfs
MKFS_FAT=$(command -v mkfs.fat 2>/dev/null || command -v mkdosfs 2>/dev/null || true)
if [ -z "$MKFS_FAT" ]; then
  for p in \
    /opt/homebrew/sbin/mkfs.fat /opt/homebrew/sbin/mkdosfs \
    /usr/local/sbin/mkfs.fat   /usr/local/sbin/mkdosfs \
    /opt/homebrew/bin/mkfs.fat  /opt/homebrew/bin/mkdosfs \
    /usr/local/bin/mkfs.fat     /usr/local/bin/mkdosfs; do
    if [ -x "$p" ]; then MKFS_FAT="$p"; break; fi
  done
fi
if [ -z "$MKFS_FAT" ]; then
  echo "error: mkfs.fat/mkdosfs not found; install dosfstools" >&2
  exit 1
fi

# Locate mtools helpers
HAVE_MTOOLS=0
if command -v mformat >/dev/null 2>&1 && command -v mmd >/dev/null 2>&1 && command -v mcopy >/dev/null 2>&1; then
  HAVE_MTOOLS=1
fi

rm -rf "$staging_dir"
mkdir -p "$staging_dir/SUBDIR"
mkdir -p "$staging_dir/longnamedir"

# Short 8.3 files
printf 'hello from auxv6 fat32 image\n' > "$staging_dir/HELLO.TXT"
printf 'subdirectory note from fat32 image\n' > "$staging_dir/SUBDIR/NOTE.TXT"
printf '0123456789\n' > "$staging_dir/NUMBERS.TXT"

# Long filename files (>8.3 so LFN entries will be generated)
printf 'this is a file with a long filename\n' > "$staging_dir/longfilename.txt"
printf 'file inside long-name directory\n' > "$staging_dir/longnamedir/readme.txt"
printf 'another long filename test file\n' > "$staging_dir/another-long-name-file.txt"

rm -f "$image"
# 128 MB: comfortably above the 65525-cluster FAT32 threshold at 512B sectors
# with 1 sector/cluster (min cluster for mkfs.fat on small images is 2, so
# 65536 clusters * 2 sectors = 131072 sectors = 64 MiB; use 128 MiB to be safe).
dd if=/dev/zero of="$image" bs=1M count=128 status=none

# Force FAT32 explicitly with -F 32
"$MKFS_FAT" -F 32 -n FAT32TEST "$image"

if [ "$HAVE_MTOOLS" -eq 1 ]; then
  mmd    -i "$image" ::/SUBDIR
  mmd    -i "$image" ::/longnamedir
  mcopy  -i "$image" "$staging_dir/HELLO.TXT"              ::/HELLO.TXT
  mcopy  -i "$image" "$staging_dir/SUBDIR/NOTE.TXT"        ::/SUBDIR/NOTE.TXT
  mcopy  -i "$image" "$staging_dir/NUMBERS.TXT"            ::/NUMBERS.TXT
  mcopy  -i "$image" "$staging_dir/longfilename.txt"       ::/longfilename.txt
  mcopy  -i "$image" "$staging_dir/longnamedir/readme.txt" ::/longnamedir/readme.txt
  mcopy  -i "$image" "$staging_dir/another-long-name-file.txt" ::/another-long-name-file.txt
else
  echo "warning: mtools not found; FAT32 image will have no seeded files" >&2
fi
