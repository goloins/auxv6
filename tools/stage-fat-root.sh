#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <staging-dir> <image>" >&2
  exit 1
fi

staging_dir="$1"
image="$2"

rm -rf "$staging_dir"
mkdir -p "$staging_dir/SUBDIR"

cat > "$staging_dir/HELLO.TXT" <<'EOF'
hello from auxv6 fat image
EOF

cat > "$staging_dir/SUBDIR/NOTE.TXT" <<'EOF'
subdirectory note from fat image
EOF

cat > "$staging_dir/NUMBERS.TXT" <<'EOF'
0123456789
EOF

rm -f "$image"
dd if=/dev/zero of="$image" bs=1M count=16 status=none
mformat -i "$image" ::
mmd -i "$image" ::/SUBDIR
mcopy -i "$image" "$staging_dir/HELLO.TXT" ::/HELLO.TXT
mcopy -i "$image" "$staging_dir/SUBDIR/NOTE.TXT" ::/SUBDIR/NOTE.TXT
mcopy -i "$image" "$staging_dir/NUMBERS.TXT" ::/NUMBERS.TXT
