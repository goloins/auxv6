#!/bin/sh

set -eu

if [ "$#" -lt 3 ]; then
  echo "usage: $0 <rootdir> <image> <files...>" >&2
  exit 1
fi

rootdir=$1
image=$2
shift 2

build_ext2_image() {
  if command -v genext2fs >/dev/null 2>&1; then
    rm -f "$image"
    genext2fs -b 8192 -N 512 -d "$rootdir" "$image"
    return 0
  fi

  if command -v mke2fs >/dev/null 2>&1; then
    rm -f "$image"
    mke2fs -q -t ext2 -d "$rootdir" -F "$image" 8192
    return 0
  fi

  if command -v mkfs.ext2 >/dev/null 2>&1; then
    rm -f "$image"
    mkfs.ext2 -q -t ext2 -d "$rootdir" -F "$image" 8192
    return 0
  fi

  echo "genext2fs, mke2fs, or mkfs.ext2 is required to build $image" >&2
  return 1
}

rm -rf "$rootdir"
install -d -m 0755 "$rootdir"
install -d -m 0755 "$rootdir/bin" "$rootdir/sbin" "$rootdir/etc" "$rootdir/dev"
install -d -m 0755 "$rootdir/home" "$rootdir/home/aux" "$rootdir/proc" "$rootdir/mnt"
install -d -m 0700 "$rootdir/root"

for src in "$@"; do
  base=${src##*/}
  case "$base" in
    _init)
      install -m 0755 "$src" "$rootdir/init"
      install -m 0755 "$src" "$rootdir/bin/init"
      ;;
    _sh)
      install -m 0755 "$src" "$rootdir/bin/6sh"
      install -m 0755 "$src" "$rootdir/bin/sh"
      ;;
    _chmod)
      install -m 0755 "$src" "$rootdir/sbin/chmod"
      ;;
    _chown)
      install -m 0755 "$src" "$rootdir/sbin/chown"
      ;;
    _chgrp)
      install -m 0755 "$src" "$rootdir/sbin/chgrp"
      ;;
    _*)
      install -m 0755 "$src" "$rootdir/bin/${base#_}"
      ;;
    etc.hosts)
      install -m 0644 "$src" "$rootdir/etc/hosts"
      ;;
    etc.fstab)
      install -m 0644 "$src" "$rootdir/etc/fstab"
      ;;
    etc.fstab.ext2root)
      install -m 0644 "$src" "$rootdir/etc/fstab"
      ;;
    etc.profile)
      install -m 0644 "$src" "$rootdir/etc/profile"
      ;;
    etc.passwd)
      install -m 0644 "$src" "$rootdir/etc/passwd"
      ;;
    etc.groups)
      install -m 0644 "$src" "$rootdir/etc/groups"
      ;;
    etc.hostname)
      install -m 0644 "$src" "$rootdir/etc/hostname"
      ;;
    *)
      install -m 0644 "$src" "$rootdir/$base"
      ;;
  esac
done

build_ext2_image