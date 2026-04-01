#!/bin/dash
# System bootstrap script (SysV-like early stage)

echo "rc.S: begin"

# Mount filesystems listed in /etc/fstab.
/bin/mount /etc/fstab

# Keep basic system identity visible to shells/tools.
if [ -f /etc/hostname ]; then
  echo "rc.S: hostname $(cat /etc/hostname)"
fi

echo "rc.S: done"
