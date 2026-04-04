#!/bin/sh
set -eu

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <stage-dir> <image-path>" >&2
  exit 1
fi

STAGE_DIR="$1"
IMG="$2"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

dd if=/dev/zero of="$IMG" bs=1m count=128 status=none

MKFS_EXFAT=""
if command -v mkfs.exfat >/dev/null 2>&1; then
  MKFS_EXFAT="$(command -v mkfs.exfat)"
elif command -v newfs_exfat >/dev/null 2>&1; then
  MKFS_EXFAT="$(command -v newfs_exfat)"
fi

if [ -z "$MKFS_EXFAT" ]; then
  echo "error: mkfs.exfat/newfs_exfat not found; install exfatprogs (Linux) or use macOS newfs_exfat" >&2
  exit 1
fi

if [ "$(basename "$MKFS_EXFAT")" = "newfs_exfat" ]; then
  "$MKFS_EXFAT" -v AUXV6EXFAT "$IMG"
else
  "$MKFS_EXFAT" -n AUXV6EXFAT "$IMG"
fi

echo "exfat image ready: $IMG"
