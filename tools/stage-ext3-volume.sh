#!/bin/sh
# stage-ext3-volume.sh <staging-dir> <image>
# Create a small ext3 test volume with an internal journal and a few seeded
# files for auxv6 secondary-disk bring-up.
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
mkdir -p "$staging_dir/docs" "$staging_dir/subdir/deeper"

printf 'hello from auxv6 ext3 test image\n' > "$staging_dir/hello.txt"
printf 'phase0 ext3 probe-only volume\n' > "$staging_dir/docs/phase0.txt"
printf 'nested ext3 file\n' > "$staging_dir/subdir/deeper/nested.txt"
printf '1234567890abcdefghijklmnopqrstuvwxyz\n' > "$staging_dir/alnum.txt"

rm -f "$image"
"$MKE2FS" -q -t ext3 -b 1024 -L AUXV6EXT3 -d "$staging_dir" -F "$image" 65536