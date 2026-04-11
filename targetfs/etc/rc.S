#!/bin/dash
# System bootstrap script (SysV-like early stage)

echo "rc.S: begin"

# Populate /dev with device nodes based on kernel inventory
echo "rc.S: running devman to create device nodes"
/bin/devman -s

# Mount filesystems listed in /etc/fstab.
/bin/mount /etc/fstab

# Keep procfs available even if the generic fstab pass failed or was skipped.
if [ ! -r /proc/uptime ]; then
  /bin/mount /proc procfs rw
fi

# Keep basic system identity visible to shells/tools.
if [ -f /etc/hostname ]; then
  echo "rc.S: hostname $(cat /etc/hostname)"
fi

echo "rc.S: done"
