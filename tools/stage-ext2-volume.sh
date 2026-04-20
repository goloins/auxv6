#!/bin/sh
# stage-ext2-volume.sh <staging-dir> <image>
# Create a small seeded ext2 test volume for auxv6 secondary-disk mount-matrix
# validation.
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <staging-dir> <image>" >&2
  exit 1
fi

staging_dir="$1"
image="$2"

MKE2FS=$(command -v mke2fs 2>/dev/null || true)
if [ -z "$MKE2FS" ] && [ -x /sbin/mke2fs ]; then
  MKE2FS=/sbin/mke2fs
fi
if [ -z "$MKE2FS" ]; then
  echo "error: mke2fs not found; install e2fsprogs" >&2
  exit 1
fi

rm -rf "$staging_dir"
mkdir -p "$staging_dir/docs"

printf 'hello from auxv6 ext2 test image\n' > "$staging_dir/hello.txt"
printf 'phase0 ext2 control volume\n' > "$staging_dir/docs/phase0.txt"
printf 'ext2 rw sentinel\n' > "$staging_dir/rw.txt"

rm -f "$image"
"$MKE2FS" -q -t ext2 -b 1024 -L AUXV6EXT2 -d "$staging_dir" -F "$image" 65536