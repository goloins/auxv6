#!/bin/sh
# stage-ext3-recovery-volume.sh <staging-dir> <image>
# Create an ext3 image that advertises needs_recovery so auxv6 can exercise
# its current hard-reject path before replay support exists.
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

printf 'hello from auxv6 ext3 recovery test image\n' > "$staging_dir/hello.txt"
printf 'phase0 ext3 recovery-required volume\n' > "$staging_dir/docs/phase0.txt"
printf 'this image should be rejected until replay exists\n' > "$staging_dir/docs/recovery.txt"
printf 'nested ext3 recovery file\n' > "$staging_dir/subdir/deeper/nested.txt"

rm -f "$image"
"$MKE2FS" -q -t ext3 -b 1024 -L AUXV6X3RECOV -d "$staging_dir" -F "$image" 65536

# ext superblock starts at byte 1024. The incompat feature word sits 96 bytes
# into that superblock, so patching byte offset 1120 flips needs_recovery.
feature_offset=1120
feature_incompat=$(od -An -tu4 -N4 -j "$feature_offset" "$image" | tr -d '[:space:]')
feature_incompat=$((feature_incompat | 0x4))

perl -e '
  use strict;
  use warnings;
  my ($path, $offset, $value) = @ARGV;
  open my $fh, "+<", $path or die "open $path: $!";
  binmode $fh;
  seek $fh, $offset, 0 or die "seek $path: $!";
  print {$fh} pack("V", $value) or die "write $path: $!";
  close $fh or die "close $path: $!";
' "$image" "$feature_offset" "$feature_incompat"