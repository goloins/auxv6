#!/bin/bash

cd /home/dakota/auxv6

# Start QEMU in background
qemu-system-i386 \
  -drive file=aux.bootkern,index=0,media=disk,format=raw \
  -drive file=fs.img,index=2,media=disk,format=raw \
  -m 768 -display none -serial stdio \
  -monitor none 2>&1 | {
    # Wait for boot
    sleep 3
    
    # Send test commands
    echo "Testing symlinks..."
    echo ""
    echo "Creating link: ln -s /bin/sh /tmp/mysh"
    echo "ln -s /bin/sh /tmp/mysh"
    sleep 1
    
    echo ""
    echo "Testing symlink:"
    echo "ls -la /tmp/mysh"
    sleep 1
    
    echo ""
    echo "Running symlinktest:"
    echo "symlinktest"
    sleep 2
    
    echo ""
    echo "Exiting..."
    echo "exit"
    
    # Keep reading output for a bit
    sleep 2
  }

