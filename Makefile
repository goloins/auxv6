OBJS = \
	kernel/core/blockdev.o\
	kernel/fs/bio.o\
	kernel/driver/console.o\
	kernel/driver/pty.o\
	kernel/core/exec.o\
	kernel/fs/file.o\
	kernel/fs/fs.o\
	kernel/fs/vfs.o\
	kernel/fs/vfs_xv6fs.o\
	kernel/fs/procfs.o\
	kernel/fs/vfs_ext2.o\
	kernel/fs/vfs_msdosfs.o\
	kernel/fs/vfs_isofs.o\
	kernel/driver/ide.o\
	kernel/driver/ioapic.o\
	kernel/driver/pci.o\
	kernel/driver/dma.o\
	kernel/driver/virtio.o\
	kernel/driver/virtio_blk.o\
	kernel/driver/virtio_gpu.o\
	kernel/driver/virtio_net.o\
	kernel/driver/ahci.o\
	kernel/driver/nvme.o\
	kernel/driver/e1000.o\
	kernel/driver/i219.o\
	kernel/driver/i226.o\
	kernel/driver/ax88179_pci.o\
	kernel/driver/pcnet.o\
	kernel/driver/rtl8111.o\
	kernel/driver/vmxnet3.o\
	kernel/driver/netvsc.o\
	kernel/driver/loop.o\
	kernel/core/kalloc.o\
	kernel/driver/kbd.o\
	kernel/driver/lapic.o\
	kernel/fs/log.o\
	kernel/core/main.o\
	kernel/driver/mp.o\
	kernel/driver/picirq.o\
	kernel/core/pipe.o\
	kernel/core/proc.o\
	kernel/core/sleeplock.o\
	kernel/core/spinlock.o\
	kernel/core/string.o\
	kernel/core/swtch.o\
	kernel/core/syscall.o\
	kernel/core/sysfile.o\
	kernel/core/sysproc.o\
	kernel/core/trap.o\
	kernel/core/trapasm.o\
	kernel/driver/uart.o\
	kernel/core/vectors.o\
	kernel/core/vm.o\
	kernel/graphics/framebuffer.o\
	kernel/graphics/display.o\
	kernel/graphics/font.o\
	kernel/graphics/render.o\
	kernel/net/socket.o\
	kernel/net/device.o\
	kernel/net/route.o\
	kernel/net/loopback.o\
	kernel/net/ethernet.o\
	kernel/net/arp.o\
	kernel/net/ip.o\
	kernel/net/icmp.o\
	kernel/net/udp.o\
	kernel/net/tcp.o\

# Cross-compiling (e.g., on Mac OS X)
# TOOLPREFIX = i386-jos-elf

# Using native tools (e.g., on X86 Linux)
#TOOLPREFIX = 

# Optional explicit cross toolchain root (default for local macOS setup).
CROSS_ROOT ?= /opt/cross
CROSS_BINDIR ?= $(CROSS_ROOT)/bin

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if i386-jos-elf-objdump -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
	then echo 'i386-jos-elf-'; \
	elif test -x '$(CROSS_BINDIR)/i386-jos-elf-objdump' && '$(CROSS_BINDIR)/i386-jos-elf-objdump' -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
		then echo '$(CROSS_BINDIR)/i386-jos-elf-'; \
	elif test -x '$(CROSS_BINDIR)/i386-elf-objdump' && '$(CROSS_BINDIR)/i386-elf-objdump' -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
	then echo '$(CROSS_BINDIR)/i386-elf-'; \
	elif test -x '$(CROSS_BINDIR)/i686-elf-objdump' && '$(CROSS_BINDIR)/i686-elf-objdump' -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
	then echo '$(CROSS_BINDIR)/i686-elf-'; \
	elif objdump -i 2>&1 | grep 'elf32-i386' >/dev/null 2>&1; \
	then echo ''; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find an i386-*-elf version of GCC/binutils." 1>&2; \
	echo "*** Is the directory with i386-jos-elf-gcc in your PATH?" 1>&2; \
	echo "*** If your i386-*-elf toolchain is installed with a command" 1>&2; \
	echo "*** prefix other than 'i386-jos-elf-', set your TOOLPREFIX" 1>&2; \
	echo "*** environment variable to that prefix and run 'make' again." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

# If the makefile can't find QEMU, specify its path here
# QEMU = qemu-system-i386

# Try to infer the correct QEMU
ifndef QEMU
QEMU = $(shell if which qemu > /dev/null; \
	then echo qemu; exit; \
	elif which qemu-system-i386 > /dev/null; \
	then echo qemu-system-i386; exit; \
	elif which qemu-system-x86_64 > /dev/null; \
	then echo qemu-system-x86_64; exit; \
	else \
	qemu=/Applications/Q.app/Contents/MacOS/i386-softmmu.app/Contents/MacOS/i386-softmmu; \
	if test -x $$qemu; then echo $$qemu; exit; fi; fi; \
	echo "***" 1>&2; \
	echo "*** Error: Couldn't find a working QEMU executable." 1>&2; \
	echo "*** Is the directory containing the qemu binary in your PATH" 1>&2; \
	echo "*** or have you tried setting the QEMU variable in Makefile?" 1>&2; \
	echo "***" 1>&2; exit 1)
endif

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump
CFLAGS = -fno-pic -static -fno-builtin -fno-strict-aliasing -O2 -Wall -MD -ggdb -m32 -Werror -fno-omit-frame-pointer -Iinclude
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)
ASFLAGS = -m32 -gdwarf-2 -Wa,-divide
# FreeBSD ld wants ``elf_i386_fbsd''
LDFLAGS += -m $(shell $(LD) -V | grep elf_i386 2>/dev/null | head -n 1)
LIBGCC := $(shell $(CC) -m32 -print-libgcc-file-name)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

# Default: ext2 root filesystem for easier dynamic modifications
ROOTFS_TYPE_VALUE ?= 2
ROOTFS_DEV_VALUE ?= 2
# Alternative: xv6 root filesystem (original default)
XV6ROOT_TYPE_VALUE ?= 1
XV6ROOT_DEV_VALUE ?= 1

ROOTFS_CONFIG = include/rootfs_config.h

.PHONY: FORCE
FORCE:

$(ROOTFS_CONFIG): FORCE Makefile
	@tmp="$@.tmp"; \
	printf '/* generated by Makefile */\n#ifndef AUXV6_ROOTFS_CONFIG_H\n#define AUXV6_ROOTFS_CONFIG_H\n#define ROOTFS_TYPE_XV6FS 1\n#define ROOTFS_TYPE_EXT2 2\n#define ROOTFS_TYPE %s\n#define ROOTFS_DEV %s\n#endif\n' \
	  "$(ROOTFS_TYPE_VALUE)" "$(ROOTFS_DEV_VALUE)" > "$$tmp"; \
	if ! cmp -s "$$tmp" "$@" 2>/dev/null; then mv "$$tmp" "$@"; else rm -f "$$tmp"; fi

$(OBJS) kernel/core/entry.o: $(ROOTFS_CONFIG)

user/%.o: $(ROOTFS_CONFIG)

xv6.img: bootblock aux.kern
	dd if=/dev/zero of=xv6.img count=10000
	dd if=bootblock of=xv6.img conv=notrunc
	dd if=aux.kern of=xv6.img seek=1 conv=notrunc

xv6memfs.img: bootblock kernelmemfs
	dd if=/dev/zero of=xv6memfs.img count=10000
	dd if=bootblock of=xv6memfs.img conv=notrunc
	dd if=kernelmemfs of=xv6memfs.img seek=1 conv=notrunc

bootblock: kernel/boot/bootasm.S kernel/boot/bootmain.c
	$(CC) $(CFLAGS) -fno-pic -O -nostdinc -Iinclude -c kernel/boot/bootmain.c -o bootmain.o
	$(CC) $(CFLAGS) -fno-pic -nostdinc -Iinclude -c kernel/boot/bootasm.S -o bootasm.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0x7C00 -o bootblock.o bootasm.o bootmain.o
	$(OBJDUMP) -S bootblock.o > bootblock.asm
	$(OBJCOPY) -S -O binary -j .text bootblock.o bootblock
	./tools/sign.pl bootblock

entryother: kernel/boot/entryother.S
	$(CC) $(CFLAGS) -fno-pic -nostdinc -Iinclude -c kernel/boot/entryother.S -o entryother.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0x7000 -o bootblockother.o entryother.o
	$(OBJCOPY) -S -O binary -j .text bootblockother.o entryother
	$(OBJDUMP) -S bootblockother.o > entryother.asm

aux.kern: $(OBJS) kernel/core/entry.o entryother config/kernel.ld
	$(LD) $(LDFLAGS) -T config/kernel.ld -o aux.kern kernel/core/entry.o $(OBJS) -b binary entryother
	$(OBJDUMP) -S aux.kern > kernel.asm
	$(OBJDUMP) -t aux.kern | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > kernel.sym

# kernelmemfs is a copy of kernel that maintains the
# disk image in memory instead of writing to a disk.
# This is not so useful for testing persistent storage or
# exploring disk buffering implementations, but it is
# great for testing the kernel on real hardware without
# needing a scratch disk.
MEMFSOBJS = $(filter-out kernel/driver/ide.o,$(OBJS)) kernel/driver/memide.o
kernelmemfs: $(MEMFSOBJS) kernel/core/entry.o entryother config/kernel.ld fs.img
	$(LD) $(LDFLAGS) -T config/kernel.ld -o kernelmemfs kernel/core/entry.o  $(MEMFSOBJS) -b binary entryother fs.img
	$(OBJDUMP) -S kernelmemfs > kernelmemfs.asm
	$(OBJDUMP) -t kernelmemfs | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > kernelmemfs.sym

tags: $(OBJS) kernel/boot/entryother.S user/_init
	etags kernel/**/*.S kernel/**/*.c user/*.c

kernel/core/vectors.S: tools/vectors.pl
	./tools/vectors.pl > kernel/core/vectors.S

ULIB = user/ulib.o user/usys.o user/printf.o user/umalloc.o user/resolve.o user/posix.o

USER_STAGE_DIR = user/.stage

# sh is close to xv6 MAXFILE; compile with -Os to keep the binary under limit.
user/sh.o: user/sh.c
	$(CC) $(CFLAGS) -Os -c -o $@ $<

# usertests is also close to xv6 MAXFILE once shared userland grows.
user/usertests.o: user/usertests.c
	$(CC) $(CFLAGS) -Os -c -o $@ $<

user/%: user/%.o $(ULIB)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^ $(LIBGCC)
	$(OBJDUMP) -S $@ > $(basename $@).asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(basename $@).sym

$(USER_STAGE_DIR):
	mkdir -p $(USER_STAGE_DIR)

$(USER_STAGE_DIR)/%: user/%.o $(ULIB) | $(USER_STAGE_DIR)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^ $(LIBGCC)

_cat: user/cat
	cp user/cat _cat

_echo: user/echo
	cp user/echo _echo

_fatregress: user/fatregress
	cp user/fatregress _fatregress

_fsregress: user/fsregress
	cp user/fsregress _fsregress

_grep: user/grep
	cp user/grep _grep

_init: user/init
	cp user/init _init

_v6init: user/v6init
	cp user/v6init _v6init

_id: user/id
	cp user/id _id

_kill: user/kill
	cp user/kill _kill

_killall: user/killall
	cp user/killall _killall

_login: user/login
	cp user/login _login

_getty: user/getty
	cp user/getty _getty

_chvt: user/chvt
	cp user/chvt _chvt

_ln: user/ln
	cp user/ln _ln

_ls: user/ls
	cp user/ls _ls

_lsblk: user/lsblk
	cp user/lsblk _lsblk

_free: user/free
	cp user/free _free

_df: user/df
	cp user/df _df

_ps: user/ps
	cp user/ps _ps

_lspci: user/lspci
	cp user/lspci _lspci

_mkdir: user/mkdir
	cp user/mkdir _mkdir

_netcat: $(USER_STAGE_DIR)/netcat
	cp $(USER_STAGE_DIR)/netcat _netcat

_telnet: $(USER_STAGE_DIR)/telnet
	cp $(USER_STAGE_DIR)/telnet _telnet

_runlevel: $(USER_STAGE_DIR)/runlevel
	cp $(USER_STAGE_DIR)/runlevel _runlevel

_telinit: $(USER_STAGE_DIR)/telinit
	cp $(USER_STAGE_DIR)/telinit _telinit

_mounts: user/mounts
	cp user/mounts _mounts

_mounttest: user/mounttest
	cp user/mounttest _mounttest

_mount: user/mount
	cp user/mount _mount

_mv: user/mv
	cp user/mv _mv

_umount: user/umount
	cp user/umount _umount

_uname: user/uname
	cp user/uname _uname

_pwd: user/pwd
	cp user/pwd _pwd

_rm: user/rm
	cp user/rm _rm

_reset: user/reset
	cp user/reset _reset

_clear: user/clear
	cp user/clear _clear

_sh: user/sh
	cp user/sh _sh

_sockettest: user/sockettest
	cp user/sockettest _sockettest

_su: user/su
	cp user/su _su

_whoami: user/whoami
	cp user/whoami _whoami

_tcptest: user/tcptest
	cp user/tcptest _tcptest

_ping: user/ping
	cp user/ping _ping

_nslookup: user/nslookup
	cp user/nslookup _nslookup

_netinfo: user/netinfo
	cp user/netinfo _netinfo

_ifconfig: user/ifconfig
	cp user/ifconfig _ifconfig

_netstat: user/netstat
	cp user/netstat _netstat

_route: user/route
	cp user/route _route

_arp: user/arp
	cp user/arp _arp

_rarp: user/rarp
	cp user/rarp _rarp

_ip: user/ip
	cp user/ip _ip

_v6dhcpd: user/v6dhcpd
	cp user/v6dhcpd _v6dhcpd

_passwd: user/passwd
	cp user/passwd _passwd

_chmod: user/chmod
	cp user/chmod _chmod

_chown: user/chown
	cp user/chown _chown

_chgrp: user/chgrp
	cp user/chgrp _chgrp

_stressfs: user/stressfs
	cp user/stressfs _stressfs

_sigtest: user/sigtest
	cp user/sigtest _sigtest

_usertests: user/usertests
	cp user/usertests _usertests

_wc: user/wc
	cp user/wc _wc

_zombie: user/zombie
	cp user/zombie _zombie

_losetup: user/losetup
	cp user/losetup _losetup

_isotest: user/isotest
	cp user/isotest _isotest

_termdemo: user/termdemo
	cp user/termdemo _termdemo

_termcheck: user/termcheck
	cp user/termcheck _termcheck

_tail: user/tail
	cp user/tail _tail

_date: user/date
	cp user/date _date

_time: user/time
	cp user/time _time

_dmesg: user/dmesg
	cp user/dmesg _dmesg

_dash: ports/dash-0.5.12/Makefile.auxv6 user/ulib.o user/usys.o user/printf.o user/umalloc.o user/resolve.o user/posix.o user/setjmp.o
	$(MAKE) -f ports/dash-0.5.12/Makefile.auxv6
	cp ports/dash-0.5.12/_dash _dash

mkfs: tools/mkfs.c include/fs.h
	gcc -Werror -Wall -o mkfs tools/mkfs.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: %.o

UPROGS=\
	_cat\
	_echo\
	_fatregress\
	_fsregress\
	_grep\
	_init\
	_id\
	_kill\
	_killall\
	_login\
	_getty\
	_ln\
	_ls\
	_lsblk\
	_free\
	_df\
	_ps\
	_lspci\
	_mkdir\
	_mount\
	_mounts\
	_mounttest\
	_mv\
	_umount\
	_uname\
	_pwd\
	_rm\
	_reset\
	_clear\
	_sh\
	_sockettest\
	_su\
	_whoami\
	_tcptest\
	_ping\
	_nslookup\
	_netinfo\
	_ifconfig\
	_netstat\
	_netcat\
	_telnet\
	_runlevel\
	_telinit\
	_route\
	_arp\
	_rarp\
	_ip\
	_v6dhcpd\
	_passwd\
	_chmod\
	_chown\
	_chgrp\
	_chvt\
	_stressfs\
	_sigtest\
	_usertests\
	_wc\
	_zombie\
	_losetup\
	_isotest\
	_termdemo\
	_termcheck\
	_tail\
	_date\
	_time\
	_dmesg\
	_dash\

# Old-init fallback set for machines without ports/dash.
UPROGS_OLDINIT = $(filter-out _dash _init,$(UPROGS)) _v6init

fs.img: mkfs README etc.hosts etc.fstab etc.profile etc.termcap etc.passwd etc.groups etc.hostname $(UPROGS)
	./mkfs fs.img README etc.hosts etc.fstab etc.profile etc.termcap etc.passwd etc.groups etc.hostname $(UPROGS)

# EXT2 root is now the default - these targets are included for clarity
ext2root:
	$(MAKE) xv6.img $(EXT2IMG)

qemu-ext2root:
	$(MAKE) xv6.img $(EXT2IMG)
	$(QEMU) -serial mon:stdio -drive file=xv6.img,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-ext2root:
	$(MAKE) xv6.img $(EXT2IMG)
	$(QEMU) -nographic -drive file=xv6.img,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-gdb-ext2root: .gdbinit
	$(MAKE) xv6.img $(EXT2IMG)
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -serial mon:stdio -drive file=xv6.img,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw -smp $(CPUS) -m 512 -S $(QEMUGDB) $(QEMUEXTRA)

qemu-nox-gdb-ext2root: .gdbinit
	$(MAKE) xv6.img $(EXT2IMG)
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -nographic -drive file=xv6.img,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw -smp $(CPUS) -m 512 -S $(QEMUGDB) $(QEMUEXTRA)

# XV6 root filesystem variants (original default, kept for compatibility)
xv6root:
	$(MAKE) ROOTFS_TYPE_VALUE=$(XV6ROOT_TYPE_VALUE) ROOTFS_DEV_VALUE=$(XV6ROOT_DEV_VALUE) xv6.img fs.img

qemu-xv6root:
	$(MAKE) ROOTFS_TYPE_VALUE=$(XV6ROOT_TYPE_VALUE) ROOTFS_DEV_VALUE=$(XV6ROOT_DEV_VALUE) xv6.img fs.img
	$(QEMU) -serial mon:stdio -drive file=xv6.img,index=0,media=disk,format=raw -drive file=fs.img,index=1,media=disk,format=raw -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-xv6root:
	$(MAKE) ROOTFS_TYPE_VALUE=$(XV6ROOT_TYPE_VALUE) ROOTFS_DEV_VALUE=$(XV6ROOT_DEV_VALUE) xv6.img fs.img
	$(QEMU) -nographic -drive file=xv6.img,index=0,media=disk,format=raw -drive file=fs.img,index=1,media=disk,format=raw -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-gdb-xv6root: .gdbinit
	$(MAKE) ROOTFS_TYPE_VALUE=$(XV6ROOT_TYPE_VALUE) ROOTFS_DEV_VALUE=$(XV6ROOT_DEV_VALUE) xv6.img fs.img
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -serial mon:stdio -drive file=xv6.img,index=0,media=disk,format=raw -drive file=fs.img,index=1,media=disk,format=raw -smp $(CPUS) -m 512 -S $(QEMUGDB) $(QEMUEXTRA)

qemu-nox-gdb-xv6root: .gdbinit
	$(MAKE) ROOTFS_TYPE_VALUE=$(XV6ROOT_TYPE_VALUE) ROOTFS_DEV_VALUE=$(XV6ROOT_DEV_VALUE) xv6.img fs.img
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -nographic -drive file=xv6.img,index=0,media=disk,format=raw -drive file=fs.img,index=1,media=disk,format=raw -smp $(CPUS) -m 512 -S $(QEMUGDB) $(QEMUEXTRA)

-include kernel/**/*.d
-include user/*.d
-include **/*.d

clean: 
	rm -rf *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*.o *.d *.asm *.sym kernel/core/vectors.S bootblock entryother \
	aux.kern xv6.img fs.img kernelmemfs \
	xv6memfs.img mkfs .gdbinit $(ROOTFS_CONFIG) \
	test_ext2.img \
	test_ext2_oldinit.img \
	test_fat.img \
	$(UPROGS) \
	$(UPROGS_OLDINIT) \
	.ext2root \
	.ext2root-oldinit \
	.fatroot \
	kernel/**/*.o kernel/**/*.d kernel/**/*.asm \
	user/*.o user/*.d user/*.asm user/cat user/echo \
	user/fatregress \
	user/fsregress \
	user/grep user/id user/init user/kill user/ln user/ls user/lsblk user/free user/df user/ps user/mkdir user/mv \
	$(USER_STAGE_DIR) \
	user/runlevel user/telinit \
	user/mount user/mounts user/mounttest user/umount \
	user/losetup user/isotest \
	user/uname \
	_dhcp \
	user/ifconfig user/netstat user/route user/arp user/rarp user/ip \
	user/dhcp user/v6dhcpd user/nslookup \
	user/date user/time user/killall \
	user/passwd user/pwd user/chmod user/chown user/chgrp user/rm user/reset user/clear user/sh user/sigtest user/sockettest user/su user/whoami user/tcptest user/ping user/netinfo user/stressfs user/usertests user/wc user/zombie user/login user/getty user/chvt user/termdemo user/termcheck user/dmesg user/tail user/lspci user/v6init

# make a printout
FILES = $(shell grep -v '^\#' tools/runoff.list)
PRINT = tools/runoff.list tools/runoff.spec README config/toc.hdr config/toc.ftr $(FILES)

xv6.pdf: $(PRINT)
	./tools/runoff
	ls -l xv6.pdf

print: xv6.pdf

# run in emulators

bochs : fs.img xv6.img
	if [ ! -e .bochsrc ]; then ln -s config/dot-bochsrc .bochsrc; fi
	bochs -q

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 2
endif
comma := ,
EXT2IMG ?= test_ext2.img
EXT2ROOT_FSTAB ?= etc.fstab.ext2root
FATIMG ?= test_fat.img
FATROOT_STAGE ?= .fatroot
# qemu* targets already depend on $(EXT2IMG), so always attach it as index=2.
EXT2QEMU = -drive file=$(EXT2IMG)$(comma)index=2$(comma)media=disk$(comma)format=raw
FATQEMU = -drive file=$(FATIMG)$(comma)index=3$(comma)media=disk$(comma)format=raw
QEMUNETOPTS ?= -netdev user,id=auxnet0 -device virtio-net-pci,netdev=auxnet0,mac=52:54:00:12:34:56,disable-modern=on
QEMUGFXOPTS ?= -device virtio-gpu-pci
QEMUOPTS = -drive file=fs.img,index=1,media=disk,format=raw -drive file=xv6.img,index=0,media=disk,format=raw $(EXT2QEMU) $(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

test_ext2.img: tools/stage-ext2-root.sh README etc.hosts $(EXT2ROOT_FSTAB) etc.profile etc.termcap etc.rc.S etc.rc.0 etc.rc.1 etc.rc.2 etc.rc.3 etc.rc.6 etc.passwd etc.groups etc.hostname etc.resolv.conf $(UPROGS)
	sh tools/stage-ext2-root.sh .ext2root $(EXT2IMG) README etc.hosts $(EXT2ROOT_FSTAB) etc.profile etc.termcap etc.rc.S etc.rc.0 etc.rc.1 etc.rc.2 etc.rc.3 etc.rc.6 etc.passwd etc.groups etc.hostname etc.resolv.conf $(UPROGS)

test_ext2_oldinit.img: tools/stage-ext2-root.sh README etc.hosts $(EXT2ROOT_FSTAB) etc.profile etc.termcap etc.passwd etc.groups etc.hostname etc.resolv.conf $(UPROGS_OLDINIT)
	sh tools/stage-ext2-root.sh .ext2root-oldinit test_ext2_oldinit.img README etc.hosts $(EXT2ROOT_FSTAB) etc.profile etc.termcap etc.passwd etc.groups etc.hostname etc.resolv.conf $(UPROGS_OLDINIT)

test_fat.img: tools/stage-fat-root.sh
	sh tools/stage-fat-root.sh $(FATROOT_STAGE) $(FATIMG)

ext2-reset:
	rm -f $(EXT2IMG)
	$(MAKE) $(EXT2IMG)

fat-reset:
	rm -f $(FATIMG)
	$(MAKE) $(FATIMG)

# Default: EXT2 root filesystem (easier to modify/mount from other systems)
qemu: xv6.img $(EXT2IMG)
	$(QEMU) -serial mon:stdio -drive file=xv6.img,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-oldinit: xv6.img test_ext2_oldinit.img
	$(QEMU) -serial mon:stdio -drive file=xv6.img,index=0,media=disk,format=raw -drive file=test_ext2_oldinit.img,index=2,media=disk,format=raw $(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-memfs: xv6memfs.img
	$(QEMU) -drive file=xv6memfs.img,index=0,media=disk,format=raw $(QEMUNETOPTS) -smp $(CPUS) -m 256

qemu-nox: xv6.img $(EXT2IMG)
	$(QEMU) -nographic -drive file=xv6.img,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-fat: xv6.img $(EXT2IMG) $(FATIMG)
	$(QEMU) -serial mon:stdio -drive file=xv6.img,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(FATQEMU) $(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-fat: xv6.img $(EXT2IMG) $(FATIMG)
	$(QEMU) -nographic -drive file=xv6.img,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(FATQEMU) $(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

.gdbinit: config/.gdbinit.tmpl
	sed "s/localhost:1234/localhost:$(GDBPORT)/" < $^ > $@

qemu-gdb: xv6.img $(EXT2IMG) .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -serial mon:stdio -drive file=xv6.img,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 -S $(QEMUGDB) $(QEMUEXTRA)

qemu-nox-gdb: xv6.img $(EXT2IMG) .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -nographic -drive file=xv6.img,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(QEMUNETOPTS) -smp $(CPUS) -m 512 -S $(QEMUGDB) $(QEMUEXTRA)

# CUT HERE
# prepare dist for students
# after running make dist, probably want to
# rename it to rev0 or rev1 or so on and then
# check in that version.

EXTRA=\
	tools/mkfs.c tools/stage-fat-root.sh user/ulib.c include/user.h user/cat.c user/echo.c user/fatregress.c user/grep.c user/kill.c\
	user/date.c user/time.c user/killall.c\
	user/id.c user/login.c user/ln.c user/ls.c user/free.c user/df.c user/ps.c user/fsregress.c user/mkdir.c user/mount.c user/mounts.c user/mounttest.c user/umount.c user/passwd.c user/pwd.c user/chmod.c user/chown.c user/chgrp.c user/rm.c user/netinfo.c user/stressfs.c user/su.c user/usertests.c user/wc.c user/whoami.c user/zombie.c\
	user/printf.c user/umalloc.c\
	README etc.hosts etc.fstab etc.profile etc.termcap etc.passwd etc.hostname config/dot-bochsrc tools/*.pl tools/toc.* tools/runoff tools/runoff1 tools/runoff.list\
	config/.gdbinit.tmpl gdbutil\

dist:
	rm -rf dist
	mkdir dist
	for i in $(FILES); \
	do \
		grep -v PAGEBREAK $$i >dist/$$i; \
	done
	sed '/CUT HERE/,$$d' Makefile >dist/Makefile
	echo >dist/runoff.spec
	cp $(EXTRA) dist

dist-test:
	rm -rf dist
	make dist
	rm -rf dist-test
	mkdir dist-test
	cp dist/* dist-test
	cd dist-test; $(MAKE) print
	cd dist-test; $(MAKE) bochs || true
	cd dist-test; $(MAKE) qemu

# update this rule (change rev#) when it is time to
# make a new revision.
tar:
	rm -rf /tmp/xv6
	mkdir -p /tmp/xv6
	cp dist/* config/.gdbinit.tmpl /tmp/xv6
	(cd /tmp; tar cf - xv6) | gzip >xv6-rev10.tar.gz  # the next one will be 10 (9/17)

.PHONY: dist-test dist ext2-reset fat-reset ext2root qemu-ext2root qemu-nox-ext2root qemu-gdb-ext2root qemu-nox-gdb-ext2root xv6root qemu-xv6root qemu-nox-xv6root qemu-gdb-xv6root qemu-nox-gdb-xv6root qemu-fat qemu-nox-fat qemu-oldinit
