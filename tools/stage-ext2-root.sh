#!/bin/sh

set -eu

if [ "$#" -lt 3 ]; then
  echo "usage: $0 <rootdir> <image> <files...>" >&2
  exit 1
fi

rootdir=$1
image=$2
shift 2

# Use fakeroot to allow chown and image tools to think they're running as root.
# This gives files root:root ownership in the ext2 image even when building as non-root.
use_fakeroot=false
if command -v fakeroot >/dev/null 2>&1; then
  use_fakeroot=true
fi

build_ext2_image() {
  local mke2fs_cmd=""
  local fakeroot_cmd=""

  # Find mke2fs (usually in /sbin on Linux)
  if [ -x /sbin/mke2fs ]; then
    mke2fs_cmd="/sbin/mke2fs"
  elif command -v mke2fs >/dev/null 2>&1; then
    mke2fs_cmd="mke2fs"
  fi

  if [ -z "$mke2fs_cmd" ]; then
    echo "mke2fs/mkfs.ext2 not found (usually in /sbin on Linux)" >&2
    return 1
  fi

  rm -f "$image"

  # Use fakeroot to build with root ownership
  if [ "$use_fakeroot" = true ]; then
    fakeroot sh -c "chown -R 0:0 '$rootdir' && '$mke2fs_cmd' -q -t ext2 -d '$rootdir' -F '$image' 32768"
  else
    # Try with sudo if not using fakeroot
    if sudo -n "$mke2fs_cmd" -q -t ext2 -d "$rootdir" -F "$image" 32768 2>/dev/null; then
      :
    else
      # Fall back to running as-is (files won't be owned by root)
      echo "warning: running mke2fs without elevated privileges; files will be owned by current user" >&2
      "$mke2fs_cmd" -q -t ext2 -d "$rootdir" -F "$image" 32768
    fi
  fi
}

cleanup_rootdir() {
  if [ ! -e "$rootdir" ]; then
    return 0
  fi

  if rm -rf "$rootdir" 2>/dev/null; then
    return 0
  fi

  if sudo -n rm -rf "$rootdir" 2>/dev/null; then
    return 0
  fi

  echo "failed to remove $rootdir; check ownership/permissions" >&2
  return 1
}

cleanup_rootdir
install -d -m 0755 "$rootdir"
install -d -m 0755 "$rootdir/bin" "$rootdir/sbin" "$rootdir/etc" "$rootdir/dev"
install -d -m 0755 "$rootdir/home" "$rootdir/home/aux" "$rootdir/proc" "$rootdir/mnt"
install -d -m 0755 "$rootdir/etc/rc.d"
install -d -m 0700 "$rootdir/root"

for src in "$@"; do
  base=${src##*/}
  case "$base" in
    _init)
      install -m 0755 "$src" "$rootdir/init"
      install -m 0755 "$src" "$rootdir/bin/init"
      ;;
    _v6init)
      install -m 0755 "$src" "$rootdir/init"
      install -m 0755 "$src" "$rootdir/bin/init"
      install -m 0755 "$src" "$rootdir/bin/v6init"
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
    _dmesg)
      install -m 0755 "$src" "$rootdir/sbin/dmesg"
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
    etc.rc.S)
      install -m 0755 "$src" "$rootdir/etc/rc.d/rc.S"
      ;;
    etc.rc.0)
      install -m 0755 "$src" "$rootdir/etc/rc.d/rc.0"
      ;;
    etc.rc.1)
      install -m 0755 "$src" "$rootdir/etc/rc.d/rc.1"
      ;;
    etc.rc.2)
      install -m 0755 "$src" "$rootdir/etc/rc.d/rc.2"
      ;;
    etc.rc.3)
      install -m 0755 "$src" "$rootdir/etc/rc.d/rc.3"
      ;;
    etc.rc.6)
      install -m 0755 "$src" "$rootdir/etc/rc.d/rc.6"
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
    etc.resolv.conf)
      install -m 0644 "$src" "$rootdir/etc/resolv.conf"
      ;;
    *)
      install -m 0644 "$src" "$rootdir/$base"
      ;;
  esac
done

build_ext2_image