#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <staging-dir> <image>" >&2
  exit 1
fi

staging_dir="$1"
image="$2"

if [ "$(uname -s)" != "Linux" ]; then
  echo "error: Btrfs image staging is only supported on Linux hosts" >&2
  exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
  echo "error: this Btrfs staging flow requires root privileges" >&2
  echo "hint: run with sudo (for example: sudo make nvme-btrfs.img)" >&2
  exit 1
fi

MKFS_BTRFS=""
for p in "$(command -v mkfs.btrfs 2>/dev/null || true)" /sbin/mkfs.btrfs /usr/sbin/mkfs.btrfs /usr/bin/mkfs.btrfs; do
  if [ -n "$p" ] && [ -x "$p" ]; then
    MKFS_BTRFS="$p"
    break
  fi
done

if [ -z "$MKFS_BTRFS" ]; then
  echo "error: mkfs.btrfs not found; install btrfs-progs" >&2
  exit 1
fi

rm -rf "$staging_dir"
mkdir -p "$staging_dir/SUBDIR"

cat > "$staging_dir/README.TXT" <<'EOF'
auxv6 btrfs test image
EOF

cat > "$staging_dir/SUBDIR/NOTE.TXT" <<'EOF'
subdirectory note from btrfs image
EOF

cat > "$staging_dir/NUMBERS.TXT" <<'EOF'
0123456789
EOF

ln -sf README.TXT "$staging_dir/README.LNK"

rm -f "$image"
truncate -s 64M "$image"

"$MKFS_BTRFS" -q -f -L AUXBTRFS --rootdir "$staging_dir" "$image"
