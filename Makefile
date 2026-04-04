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
	kernel/fs/vfs_tmpfs.o\
	kernel/fs/vfs_nfs.o\
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
	kernel/core/ktime.o\
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
	kernel/net/xdr.o\
	kernel/net/rpc.o\
	kernel/net/mount.o\
	kernel/net/nfs.o\

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
ASFLAGS = -m32 -gdwarf-2 -Wa,-divide -Iinclude
# FreeBSD ld wants ``elf_i386_fbsd''
LDFLAGS += -m $(shell $(LD) -V | grep elf_i386 2>/dev/null | head -n 1)
LIBGCC := $(shell $(CC) -m32 -print-libgcc-file-name)

# Never use host system headers for auxv6 guest code.
CFLAGS += -nostdinc

# Allow callers to append debug flags without clobbering base CFLAGS.
CFLAGS += $(EXTRA_CFLAGS)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

.PHONY: toolchain-check
toolchain-check:
	@$(CC) -m32 -dM -E -x c /dev/null | grep -q '__i386__' || \
		(echo "***" 1>&2; \
		echo "*** Error: $(CC) is not producing 32-bit i386 code for -m32." 1>&2; \
		echo "*** Set TOOLPREFIX to an i386-*-elf toolchain or install host multilib support." 1>&2; \
		echo "***" 1>&2; exit 1)
	@nm "$(LIBGCC)" 2>/dev/null | grep -Eq '__divmoddi4|__udivdi3|__divdi3' || \
		(echo "***" 1>&2; \
		echo "*** Error: selected libgcc is missing i386 64-bit division helpers." 1>&2; \
		echo "*** LIBGCC=$(LIBGCC)" 1>&2; \
		echo "*** This usually means a host 64-bit runtime archive was selected." 1>&2; \
		echo "*** Install multilib or use an i386-*-elf TOOLPREFIX." 1>&2; \
		echo "***" 1>&2; exit 1)

# Default: ext2 root filesystem for easier dynamic modifications
ROOTFS_TYPE_VALUE ?= 2
ROOTFS_DEV_VALUE ?= 2

ROOTFS_CONFIG = include/rootfs_config.h
EXTRA_CFLAGS_STAMP = .extra_cflags.stamp

.PHONY: FORCE
FORCE:

$(ROOTFS_CONFIG): FORCE Makefile
	@tmp="$@.tmp"; \
	printf '/* generated by Makefile */\n#ifndef AUXV6_ROOTFS_CONFIG_H\n#define AUXV6_ROOTFS_CONFIG_H\n#define ROOTFS_TYPE_XV6FS 1\n#define ROOTFS_TYPE_EXT2 2\n#define ROOTFS_TYPE %s\n#define ROOTFS_DEV %s\n#endif\n' \
	  "$(ROOTFS_TYPE_VALUE)" "$(ROOTFS_DEV_VALUE)" > "$$tmp"; \
	if ! cmp -s "$$tmp" "$@" 2>/dev/null; then mv "$$tmp" "$@"; else rm -f "$$tmp"; fi

$(EXTRA_CFLAGS_STAMP): FORCE Makefile
	@tmp="$@.tmp"; \
	printf '%s\n' "$(EXTRA_CFLAGS)" > "$$tmp"; \
	if ! cmp -s "$$tmp" "$@" 2>/dev/null; then mv "$$tmp" "$@"; else rm -f "$$tmp"; fi

$(OBJS) kernel/core/entry.o: $(ROOTFS_CONFIG) $(EXTRA_CFLAGS_STAMP)

user/%.o: $(ROOTFS_CONFIG) $(EXTRA_CFLAGS_STAMP)

aux.bootkern: bootblock aux.kern
	dd if=/dev/zero of=aux.bootkern count=10000
	dd if=bootblock of=aux.bootkern conv=notrunc
	dd if=aux.kern of=aux.bootkern seek=1 conv=notrunc

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

aux.kern: toolchain-check $(OBJS) kernel/core/entry.o entryother config/kernel.ld
	$(LD) $(LDFLAGS) -T config/kernel.ld -o aux.kern kernel/core/entry.o $(OBJS) -b binary entryother
	$(OBJDUMP) -S aux.kern > kernel.asm
	$(OBJDUMP) -t aux.kern | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > kernel.sym
	install -d $(TARGETFS_DIR)
	install -m 0644 aux.kern $(TARGETFS_DIR)/aux.kern

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

LIBC_OBJS = user/ulib.o user/string.o user/errstr.o user/umalloc.o user/tty.o user/inet.o user/fmt.o user/dirent.o user/fnmatch.o user/glob.o user/ftw.o user/fts.o user/locale.o user/pwdgrp.o user/env.o user/conf.o user/path.o user/tempfile.o user/timecore.o user/resource.o user/netdb.o user/stdlib.o user/posix_fs.o user/posix.o user/stdio.o user/regex.o user/calloc.o user/libterm.o
LIBAUXV6_OBJS = user/crt0.o user/usys.o user/printf.o user/resolve.o
ULIB = $(LIBC_OBJS) $(LIBAUXV6_OBJS)

USER_STAGE_DIR = user/.stage

# sh is close to xv6 MAXFILE; compile with -Os to keep the binary under limit.
user/sh.o: user/sh.c
	$(CC) $(CFLAGS) -Os -c -o $@ $<

# usertests is also close to xv6 MAXFILE once shared userland grows.
user/usertests.o: user/usertests.c
	$(CC) $(CFLAGS) -Os -c -o $@ $<

user/%: user/%.o $(ULIB) | toolchain-check
	$(LD) $(LDFLAGS) -N -e _start -Ttext 0 -o $@ $^ $(LIBGCC)
	$(OBJDUMP) -S $@ > $(basename $@).asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(basename $@).sym

$(USER_STAGE_DIR):
	mkdir -p $(USER_STAGE_DIR)

$(USER_STAGE_DIR)/%: user/%.o $(ULIB) | $(USER_STAGE_DIR) toolchain-check
	$(LD) $(LDFLAGS) -N -e _start -Ttext 0 -o $@ $^ $(LIBGCC)

_cat: user/cat
	cp user/cat _cat

_devman: user/devman
	cp user/devman _devman

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

_halt: user/halt
	cp user/halt _halt

_login: user/login
	cp user/login _login

_getty: user/getty
	cp user/getty _getty

_chvt: user/chvt
	cp user/chvt _chvt

_ln: user/ln
	cp user/ln _ln

_cp: user/cp
	cp user/cp _cp

_ls: user/ls
	cp user/ls _ls

_man: user/man
	cp user/man _man

_lsblk: user/lsblk
	cp user/lsblk _lsblk

_free: user/free
	cp user/free _free

_df: user/df
	cp user/df _df

_ps: user/ps
	cp user/ps _ps

_top: user/top
	cp user/top _top

_lspci: user/lspci
	cp user/lspci _lspci

_mkdir: user/mkdir
	cp user/mkdir _mkdir

_netcat: $(USER_STAGE_DIR)/netcat
	cp $(USER_STAGE_DIR)/netcat _netcat

_6get: user/6get
	cp user/6get _6get

_abrowse: user/abrowse
	cp user/abrowse _abrowse

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

_mktmpfs: user/mktmpfs
	cp user/mktmpfs _mktmpfs

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

_udptest: user/udptest
	cp user/udptest _udptest

_ping: user/ping
	cp user/ping _ping

_traceroute: user/traceroute
	cp user/traceroute _traceroute

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

_ntpd: user/ntpd
	cp user/ntpd _ntpd

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

_schedperf: user/schedperf
	cp user/schedperf _schedperf

_fsperf: user/fsperf
	cp user/fsperf _fsperf

_kallocstress: user/kallocstress
	cp user/kallocstress _kallocstress

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

_looptest: user/looptest
	cp user/looptest _looptest

_vblktest: user/vblktest
	cp user/vblktest _vblktest

_ahcitest: user/ahcitest
	cp user/ahcitest _ahcitest

_termdemo: user/termdemo
	cp user/termdemo _termdemo

_termcheck: user/termcheck
	cp user/termcheck _termcheck

_tail: user/tail
	cp user/tail _tail

_lsof: user/lsof
	cp user/lsof _lsof

_which: user/which
	cp user/which _which

_file: user/file
	cp user/file _file

_server7: user/server7
	cp user/server7 _server7

_date: user/date
	cp user/date _date

_time: user/time
	cp user/time _time

_dmesg: user/dmesg
	cp user/dmesg _dmesg

_dash: ports/dash-0.5.12/Makefile.auxv6 $(ULIB) user/setjmp.o
	$(MAKE) -f ports/dash-0.5.12/Makefile.auxv6
	cp ports/dash-0.5.12/_dash _dash

_symlinktest: user/symlinktest
	cp user/symlinktest _symlinktest

_nftwtest: user/nftwtest
	cp user/nftwtest _nftwtest

_ftwtest: user/ftwtest
	cp user/ftwtest _ftwtest

_ftstest: user/ftstest
	cp user/ftstest _ftstest

mkfs: tools/mkfs.c include/fs.h
	gcc -Werror -Wall -o mkfs tools/mkfs.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: %.o

UPROGS=\
	_cat\
	_man\
	_devman\
	_echo\
	_fatregress\
	_fsregress\
	_grep\
	_init\
	_id\
	_kill\
	_killall\
	_halt\
	_login\
	_getty\
	_ln\
	_cp\
	_ls\
	_lsblk\
	_free\
	_df\
	_ps\
	_top\
	_lspci\
	_mkdir\
	_mount\
	_mktmpfs\
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
	_udptest\
	_ping\
	_traceroute\
	_nslookup\
	_netinfo\
	_ifconfig\
	_netstat\
	_netcat\
	_6get\
	_telnet\
	_runlevel\
	_telinit\
	_route\
	_arp\
	_rarp\
	_ip\
	_v6dhcpd\
	_ntpd\
	_passwd\
	_chmod\
	_chown\
	_chgrp\
	_chvt\
	_stressfs\
	_schedperf\
	_fsperf\
	_kallocstress\
	_sigtest\
	_usertests\
	_wc\
	_zombie\
	_losetup\
	_isotest\
	_looptest\
	_vblktest\
	_ahcitest\
	_termdemo\
	_termcheck\
	_tail\
	_lsof\
	_which\
	_file\
	_date\
	_time\
	_dmesg\
	_server7\
	_dash\
	_symlinktest\
	_nftwtest\
	_ftwtest\
	_ftstest\

# Old-init fallback set for machines without ports/dash.
UPROGS_OLDINIT = $(filter-out _dash _init,$(UPROGS)) _v6init

fs.img: $(EXT2IMG)
	cp -f $(EXT2IMG) fs.img

# EXT2 root is now the default - these targets are included for clarity
ext2root:
	$(MAKE) aux.bootkern $(EXT2IMG)

qemu-ext2root:
	$(MAKE) aux.bootkern $(EXT2IMG)
	$(QEMU) -serial mon:stdio -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-ext2root:
	$(MAKE) aux.bootkern $(EXT2IMG)
	$(QEMU) -nographic -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-gdb-ext2root: .gdbinit
	$(MAKE) aux.bootkern $(EXT2IMG)
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -serial mon:stdio -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw -smp $(CPUS) -m 512 -S $(QEMUGDB) $(QEMUEXTRA)

qemu-nox-gdb-ext2root: .gdbinit
	$(MAKE) aux.bootkern $(EXT2IMG)
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -nographic -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw -smp $(CPUS) -m 512 -S $(QEMUGDB) $(QEMUEXTRA)

-include kernel/**/*.d
-include user/*.d
-include **/*.d

clean: 
	rm -rf *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*.o *.d *.asm *.sym kernel/core/vectors.S bootblock entryother \
	aux.kern aux.bootkern fs.img kernelmemfs \
	xv6memfs.img mkfs .gdbinit $(ROOTFS_CONFIG) \
	$(EXTRA_CFLAGS_STAMP) \
	test_ext2.img \
	test_ext2_oldinit.img \
	test_ext2_server7.img \
	test_fat.img \
	vblk0.img \
	vblk1.img \
	vblk-stress.img \
	ahci-stress.img \
	nvme-ext2.img \
	nvme-fat.img \
	$(UPROGS) \
	$(UPROGS_OLDINIT) \
	.ext2root \
	.ext2root-oldinit \
	.ext2root-server7 \
	.fatroot \
	targetfs/tmp/test.iso .isoroot \
	kernel/**/*.o kernel/**/*.d kernel/**/*.asm \
	user/*.o user/*.d user/*.asm user/cat user/echo \
	user/fatregress \
	user/fsregress \
	user/grep user/id user/init user/kill user/ln user/ls user/lsblk user/free user/df user/ps user/mkdir user/mv \
	$(USER_STAGE_DIR) \
	user/runlevel user/telinit \
	user/mount user/mounts user/mounttest user/umount \
	user/mktmpfs \
	user/losetup user/isotest user/looptest user/vblktest \
	user/ahcitest \
	user/man \
	user/uname \
	_dhcp \
	user/ifconfig user/netstat user/route user/arp user/rarp user/ip \
	user/dhcp user/v6dhcpd user/ntpd user/nslookup \
	user/6get \
	user/abrowse \
	user/lsof user/which user/file \
	user/server7 \
	user/top \
	user/date user/time user/killall user/halt \
	user/passwd user/pwd user/chmod user/chown user/chgrp user/rm user/reset user/clear user/sh user/sigtest user/sockettest user/su user/whoami user/tcptest user/ping user/netinfo user/stressfs user/usertests user/wc user/zombie user/login user/getty user/chvt user/termdemo user/termcheck user/dmesg user/tail user/lspci user/v6init
	user/schedperf user/fsperf user/kallocstress

# make a printout
FILES = $(shell grep -v '^\#' tools/runoff.list)
PRINT = tools/runoff.list tools/runoff.spec README config/toc.hdr config/toc.ftr $(FILES)

xv6.pdf: $(PRINT)
	./tools/runoff
	ls -l xv6.pdf

print: xv6.pdf

# run in emulators

bochs : aux.bootkern $(EXT2IMG)
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
TARGETFS_DIR ?= targetfs
TARGETFS_ETC ?= $(TARGETFS_DIR)/etc
TARGETFS_SBIN ?= $(TARGETFS_DIR)/sbin
TARGETFS_MAN_DIR ?= $(TARGETFS_DIR)/usr/share/man
EXT2ROOT_FSTAB ?= $(TARGETFS_ETC)/fstab.ext2root
ROOTFS_COMMON_FILES = README $(TARGETFS_ETC)/hosts $(EXT2ROOT_FSTAB) $(TARGETFS_ETC)/profile $(TARGETFS_ETC)/termcap $(TARGETFS_ETC)/passwd $(TARGETFS_ETC)/group $(TARGETFS_ETC)/hostname $(TARGETFS_ETC)/motd $(TARGETFS_ETC)/resolv.conf $(TARGETFS_SBIN)/mount.ext2 $(TARGETFS_SBIN)/mount.msdosfs $(TARGETFS_SBIN)/mount.isofs $(TARGETFS_SBIN)/mount.xv6fs $(TARGETFS_DIR)/tmp/test.iso
ROOTFS_RC_FILES = $(TARGETFS_ETC)/rc.S $(TARGETFS_ETC)/rc.0 $(TARGETFS_ETC)/rc.1 $(TARGETFS_ETC)/rc.2 $(TARGETFS_ETC)/rc.3 $(TARGETFS_ETC)/rc.6
ROOTFS_RC_FILES_SERVER7 = $(filter-out $(TARGETFS_ETC)/rc.2,$(ROOTFS_RC_FILES)) $(TARGETFS_ETC)/rc.2.server7
ROOTFS_MAN_FILES = $(wildcard $(TARGETFS_MAN_DIR)/*.md)
ROOTFS_TARGETFS_FILES = $(shell find $(TARGETFS_DIR) -type f -o -type l 2>/dev/null)
FATIMG ?= test_fat.img
FATROOT_STAGE ?= .fatroot
# qemu* targets already depend on $(EXT2IMG), so always attach it as index=2.
EXT2QEMU = -drive file=$(EXT2IMG)$(comma)index=2$(comma)media=disk$(comma)format=raw
FATQEMU = -drive file=$(FATIMG)$(comma)index=3$(comma)media=disk$(comma)format=raw
QEMUNETOPTS ?= -netdev user,id=auxnet0 -device virtio-net-pci,netdev=auxnet0,mac=52:54:00:12:34:56,disable-modern=on
QEMUNETOPTS_E1000 ?= -netdev user,id=auxnet0 -device e1000,netdev=auxnet0,mac=52:54:00:12:34:56
QEMUGFXOPTS ?= -vga none -device virtio-gpu-pci,disable-modern=on,xres=1200,yres=800
QEMUOPTS = -drive file=aux.bootkern,index=0,media=disk,format=raw $(EXT2QEMU) $(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

$(TARGETFS_DIR)/tmp/test.iso:
	mkdir -p .isoroot $(TARGETFS_DIR)/tmp
	printf 'auxv6 isofs test image\n' > .isoroot/README.TXT
	printf 'hello from auxv6 loop test\n' > .isoroot/HELLO.TXT
	printf '\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f' > .isoroot/DATA.BIN
	@if command -v mkisofs >/dev/null 2>&1; then \
		mkisofs -quiet -rock -o $@ .isoroot; \
	elif command -v genisoimage >/dev/null 2>&1; then \
		genisoimage -quiet -rock -o $@ .isoroot; \
	elif command -v xorriso >/dev/null 2>&1; then \
		xorriso -as mkisofs -quiet -rock -o $@ .isoroot; \
	else \
		echo "error: need mkisofs, genisoimage, or xorriso to build $@" >&2; \
		exit 1; \
	fi
	rm -rf .isoroot
#nice
test_ext2.img: tools/stage-ext2-root.sh $(ROOTFS_COMMON_FILES) $(ROOTFS_RC_FILES) $(ROOTFS_MAN_FILES) $(ROOTFS_TARGETFS_FILES) $(UPROGS)
	sh tools/stage-ext2-root.sh .ext2root $(EXT2IMG) $(ROOTFS_COMMON_FILES) $(ROOTFS_RC_FILES) $(ROOTFS_MAN_FILES) $(ROOTFS_TARGETFS_FILES) $(UPROGS)

test_ext2_server7.img: tools/stage-ext2-root.sh $(ROOTFS_COMMON_FILES) $(ROOTFS_RC_FILES_SERVER7) $(ROOTFS_MAN_FILES) $(ROOTFS_TARGETFS_FILES) $(UPROGS)
	sh tools/stage-ext2-root.sh .ext2root-server7 test_ext2_server7.img $(ROOTFS_COMMON_FILES) $(ROOTFS_RC_FILES_SERVER7) $(ROOTFS_MAN_FILES) $(ROOTFS_TARGETFS_FILES) $(UPROGS)

test_ext2_oldinit.img: tools/stage-ext2-root.sh $(ROOTFS_COMMON_FILES) $(UPROGS_OLDINIT)
	sh tools/stage-ext2-root.sh .ext2root-oldinit test_ext2_oldinit.img $(ROOTFS_COMMON_FILES) $(UPROGS_OLDINIT)

test_fat.img: tools/stage-fat-root.sh
	sh tools/stage-fat-root.sh $(FATROOT_STAGE) $(FATIMG)

ext2-reset:
	rm -f $(EXT2IMG)
	$(MAKE) $(EXT2IMG)

fat-reset:
	rm -f $(FATIMG)
	$(MAKE) $(FATIMG)

# Virtio-blk test images (empty ext2 filesystems for testing virtio-blk operations)
vblk0.img:
	@if command -v mke2fs >/dev/null 2>&1; then \
		mke2fs -q -t ext2 -F $@ 262144; \
	elif [ -x /sbin/mke2fs ]; then \
		/sbin/mke2fs -q -t ext2 -F $@ 262144; \
	else \
		echo "error: mke2fs not found; unable to create virtio-blk test image" >&2; \
		exit 1; \
	fi

vblk1.img:
	@if command -v mke2fs >/dev/null 2>&1; then \
		mke2fs -q -t ext2 -F $@ 262144; \
	elif [ -x /sbin/mke2fs ]; then \
		/sbin/mke2fs -q -t ext2 -F $@ 262144; \
	else \
		echo "error: mke2fs not found; unable to create virtio-blk test image" >&2; \
		exit 1; \
	fi

vblk-stress.img:
	@if command -v mke2fs >/dev/null 2>&1; then \
		mke2fs -q -t ext2 -F $@ 65536; \
	elif [ -x /sbin/mke2fs ]; then \
		/sbin/mke2fs -q -t ext2 -F $@ 65536; \
	else \
		echo "error: mke2fs not found; unable to create virtio-blk stress image" >&2; \
		exit 1; \
	fi

ahci-stress.img:
	@if command -v mke2fs >/dev/null 2>&1; then \
		mke2fs -q -t ext2 -F $@ 65536; \
	elif [ -x /sbin/mke2fs ]; then \
		/sbin/mke2fs -q -t ext2 -F $@ 65536; \
	else \
		echo "error: mke2fs not found; unable to create AHCI stress image" >&2; \
		exit 1; \
	fi

# NVMe ext2 test image: 32 MB ext2 volume for NVMe driver validation.
# Mount inside the guest with: mount -t ext2 n0 /mnt/nvme
nvme-ext2.img:
	@if command -v mke2fs >/dev/null 2>&1; then \
		mke2fs -q -t ext2 -F $@ 65536; \
	elif [ -x /sbin/mke2fs ]; then \
		/sbin/mke2fs -q -t ext2 -F $@ 65536; \
	else \
		echo "error: mke2fs not found; unable to create NVMe ext2 test image" >&2; \
		exit 1; \
	fi

# NVMe FAT test image: 16 MB FAT16 volume for NVMe + msdosfs driver validation.
# Mount inside the guest with: mount -t msdosfs n0 /mnt/nvme
nvme-fat.img:
	@MKFS_FAT=$$(command -v mkfs.fat 2>/dev/null || command -v mkdosfs 2>/dev/null || true); \
	if [ -z "$$MKFS_FAT" ]; then \
		for p in /opt/homebrew/sbin/mkfs.fat /opt/homebrew/sbin/mkdosfs /usr/local/sbin/mkfs.fat /usr/local/sbin/mkdosfs /opt/homebrew/bin/mkfs.fat /opt/homebrew/bin/mkdosfs /usr/local/bin/mkfs.fat /usr/local/bin/mkdosfs; do \
			if [ -x "$$p" ]; then MKFS_FAT="$$p"; break; fi; \
		done; \
	fi; \
	if [ -n "$$MKFS_FAT" ]; then \
		dd if=/dev/zero of=$@ bs=512 count=32768 2>/dev/null; \
		"$$MKFS_FAT" -F 16 -n NVMETEST ./$@; \
		STAMP_TEXT='auxv6 nvme-fat stamp'; \
		STAMP_SIZE=$$(printf '%s\n' "$$STAMP_TEXT" | wc -c | tr -d ' '); \
		printf '\377\377' | dd of=$@ bs=1 seek=$$((4*512 + 4)) conv=notrunc 2>/dev/null; \
		printf '\377\377' | dd of=$@ bs=1 seek=$$((36*512 + 4)) conv=notrunc 2>/dev/null; \
		perl -e 'my $$n="README  TXT"; my $$s=shift; print $$n, pack("C C C v v v v v v v V", 0x20,0,0,0,0,0,0,0,0,2,$$s);' "$$STAMP_SIZE" \
			| dd of=$@ bs=1 seek=$$((68*512 + 32)) conv=notrunc 2>/dev/null; \
		printf '%s\n' "$$STAMP_TEXT" | dd of=$@ bs=1 seek=$$((100*512)) conv=notrunc 2>/dev/null; \
	else \
		echo "error: mkfs.fat/mkdosfs not found; install dosfstools" >&2; \
		exit 1; \
	fi

vblk-reset:
	rm -f vblk0.img vblk1.img
	$(MAKE) vblk0.img vblk1.img

# Default: EXT2 root filesystem (easier to modify/mount from other systems)
qemu: aux.bootkern $(EXT2IMG)
	$(QEMU) -serial mon:stdio -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

e1000: aux.bootkern $(EXT2IMG)
	$(QEMU) -serial mon:stdio -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(QEMUNETOPTS_E1000) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-server7: aux.bootkern test_ext2_server7.img
	$(QEMU) -serial mon:stdio -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=test_ext2_server7.img,index=2,media=disk,format=raw $(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-oldinit: aux.bootkern test_ext2_oldinit.img
	$(QEMU) -serial mon:stdio -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=test_ext2_oldinit.img,index=2,media=disk,format=raw $(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-memfs: xv6memfs.img
	$(QEMU) -drive file=xv6memfs.img,index=0,media=disk,format=raw $(QEMUNETOPTS) -smp $(CPUS) -m 256

qemu-nox: aux.bootkern $(EXT2IMG)
	$(QEMU) -nographic -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-fat: aux.bootkern $(EXT2IMG) $(FATIMG)
	$(QEMU) -serial mon:stdio -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(FATQEMU) $(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-fat: aux.bootkern $(EXT2IMG) $(FATIMG)
	$(QEMU) -nographic -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(FATQEMU) $(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

.gdbinit: config/.gdbinit.tmpl
	sed "s/localhost:1234/localhost:$(GDBPORT)/" < $^ > $@

qemu-gdb: aux.bootkern $(EXT2IMG) .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -serial mon:stdio -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 -S $(QEMUGDB) $(QEMUEXTRA)

qemu-nox-gdb: aux.bootkern $(EXT2IMG) .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -nographic -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(QEMUNETOPTS) -smp $(CPUS) -m 512 -S $(QEMUGDB) $(QEMUEXTRA)

# Virtio-blk testing: attach extra volumes via virtio-blk interface for harness validation
qemu-virtioblktest: aux.bootkern $(EXT2IMG) vblk0.img vblk1.img
	$(QEMU) -serial mon:stdio \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=vblk0.img,if=virtio,format=raw \
		-drive file=vblk1.img,if=virtio,format=raw \
		$(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-virtioblktest: aux.bootkern $(EXT2IMG) vblk0.img vblk1.img
	$(QEMU) -nographic \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=vblk0.img,if=virtio,format=raw \
		-drive file=vblk1.img,if=virtio,format=raw \
		$(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

# Virtio-blk mount stress: single pre-formatted ext2 disk for mount/umount cycle testing
qemu-nox-virtioblkstress: aux.bootkern $(EXT2IMG) vblk-stress.img
	$(QEMU) -nographic \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=vblk-stress.img,if=virtio,format=raw \
		$(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

# AHCI mount stress: one extra AHCI-backed ext2 disk attached on port 3.
qemu-nox-ahcistress: aux.bootkern $(EXT2IMG) ahci-stress.img
	$(QEMU) -nographic \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-device ich9-ahci,id=ahci \
		-drive file=ahci-stress.img,if=none,id=ahcidisk,format=raw \
		-device ide-hd,drive=ahcidisk,bus=ahci.3 \
		$(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

# NVMe ext2 test: boot with an NVMe controller holding a pre-formatted ext2 volume.
# The kernel registers the NVMe device as n0 (ND_DISK_DEV(0)).
# Inside the guest:
#   mkdir /mnt/nvme && mount -t ext2 n0 /mnt/nvme
#   lsblk  (should show n0)
qemu-nvme: aux.bootkern $(EXT2IMG) nvme-ext2.img
	$(QEMU) -serial mon:stdio \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=nvme-ext2.img,if=none,id=nvme0,format=raw \
		-device nvme,drive=nvme0,serial=auxv6nvme0 \
		$(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nvme-dbg:
	$(MAKE) EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DDBG_NVME=1" qemu-nvme

qemu-nox-nvme: aux.bootkern $(EXT2IMG) nvme-ext2.img
	$(QEMU) -nographic \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=nvme-ext2.img,if=none,id=nvme0,format=raw \
		-device nvme,drive=nvme0,serial=auxv6nvme0 \
		$(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-nvme-dbg:
	$(MAKE) EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DDBG_NVME=1" qemu-nox-nvme

# NVMe FAT test: same config but with a FAT16 volume.
# Inside the guest:
#   mkdir /mnt/nvme && mount -t msdosfs n0 /mnt/nvme
qemu-nvme-fat: aux.bootkern $(EXT2IMG) nvme-fat.img
	$(QEMU) -serial mon:stdio \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=nvme-fat.img,if=none,id=nvme0,format=raw \
		-device nvme,drive=nvme0,serial=auxv6nvme0 \
		$(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

# Generic automated guest test template (extend by changing target + command file).
AUXV6_QEMU_TARGET ?= qemu-nox
AUXV6_MAKE_CMD ?=
AUXV6_TEST_SCRIPT ?=
AUXV6_EXPECT_TIMEOUT ?= 240
AUXV6_HALT ?= 1
AUXV6_CHECK_RC ?= 0

qemu-guesttest-template:
	@if ! command -v expect >/dev/null 2>&1; then \
		echo "error: expect not found; install expect to run guest automation" >&2; \
		exit 1; \
	fi
	@if [ -z "$(AUXV6_TEST_SCRIPT)" ]; then \
		echo "error: AUXV6_TEST_SCRIPT is required" >&2; \
		echo "example: make qemu-guesttest-template AUXV6_QEMU_TARGET=qemu-nox-virtioblktest AUXV6_TEST_SCRIPT=tools/tests/virtioblk-smoke.cmds" >&2; \
		exit 1; \
	fi
	@mkcmd="$(AUXV6_MAKE_CMD)"; \
	if [ -z "$$mkcmd" ]; then \
		if [ "$$(uname -s)" = "Darwin" ]; then \
			mkcmd="sudo make"; \
		else \
			mkcmd="make"; \
		fi; \
	fi; \
	_kill_qemu() { \
		if [ "$$(uname -s)" = "Darwin" ]; then \
			sudo -n killall -9 qemu-system-i386 qemu-system-x86_64 >/dev/null 2>&1 || true; \
		else \
			killall -9 qemu-system-i386 qemu-system-x86_64 >/dev/null 2>&1 || true; \
		fi; \
	}; \
	trap '_kill_qemu' EXIT INT TERM HUP; \
	AUXV6_MAKE_CMD="$$mkcmd" \
	 AUXV6_QEMU_TARGET="$(AUXV6_QEMU_TARGET)" \
	 AUXV6_TEST_SCRIPT="$(AUXV6_TEST_SCRIPT)" \
	 AUXV6_EXPECT_TIMEOUT="$(AUXV6_EXPECT_TIMEOUT)" \
	 AUXV6_CHECK_RC="$(AUXV6_CHECK_RC)" \
	 AUXV6_HALT="$(AUXV6_HALT)" \
	 expect tools/qemu-guest-test.exp; \
	rc=$$?; \
	if [ $$rc -ne 0 ]; then \
		echo "guesttest: harness failed (rc=$$rc); qemu will be killed by exit trap" >&2; \
	fi; \
	exit $$rc

test-virtioblk-smoke: aux.bootkern $(EXT2IMG) vblk0.img vblk1.img
	@$(MAKE) qemu-guesttest-template \
		AUXV6_QEMU_TARGET=qemu-nox-virtioblktest \
		AUXV6_TEST_SCRIPT=tools/tests/virtioblk-smoke.cmds

test-virtioblk-negative: aux.bootkern $(EXT2IMG) vblk0.img vblk1.img
	@$(MAKE) qemu-guesttest-template \
		AUXV6_QEMU_TARGET=qemu-nox-virtioblktest \
		AUXV6_TEST_SCRIPT=tools/tests/virtioblk-negative.cmds

test-virtioblk-retry-stress: aux.bootkern $(EXT2IMG) vblk0.img vblk1.img
	@$(MAKE) qemu-guesttest-template \
		AUXV6_QEMU_TARGET=qemu-nox-virtioblktest \
		AUXV6_TEST_SCRIPT=tools/tests/virtioblk-retry-stress.cmds

test-virtioblk-mount-stress: aux.bootkern $(EXT2IMG) vblk-stress.img
	@$(MAKE) qemu-guesttest-template \
		AUXV6_QEMU_TARGET=qemu-nox-virtioblkstress \
		AUXV6_TEST_SCRIPT=tools/tests/virtioblk-mount-stress.cmds

test-ahci-mount-stress: aux.bootkern $(EXT2IMG) ahci-stress.img
	@$(MAKE) qemu-guesttest-template \
		AUXV6_QEMU_TARGET=qemu-nox-ahcistress \
		AUXV6_TEST_SCRIPT=tools/tests/ahci-mount-stress.cmds

test-ahci-retry-stress: aux.bootkern $(EXT2IMG) ahci-stress.img
	@$(MAKE) qemu-guesttest-template \
		AUXV6_QEMU_TARGET=qemu-nox-ahcistress \
		AUXV6_TEST_SCRIPT=tools/tests/ahci-retry-stress.cmds

test-ahci-retry-stress-debug: aux.bootkern $(EXT2IMG) ahci-stress.img
	@$(MAKE) qemu-guesttest-template \
		AUXV6_QEMU_TARGET=qemu-nox-ahcistress \
		AUXV6_LOG_USER=1 \
		AUXV6_EXPECT_TIMEOUT=360 \
		AUXV6_TEST_SCRIPT=tools/tests/ahci-retry-stress-debug.cmds

test-ahci-mount-soak: aux.bootkern $(EXT2IMG) ahci-stress.img
	@$(MAKE) qemu-guesttest-template \
		AUXV6_QEMU_TARGET=qemu-nox-ahcistress \
		AUXV6_TEST_SCRIPT=tools/tests/ahci-mount-soak.cmds

test-ahci-regression: aux.bootkern $(EXT2IMG) ahci-stress.img
	@$(MAKE) test-ahci-mount-stress
	@$(MAKE) test-ahci-retry-stress
	@$(MAKE) test-ahci-mount-soak

test-termcheck-smoke: aux.bootkern $(EXT2IMG)
	@$(MAKE) qemu-guesttest-template \
		AUXV6_QEMU_TARGET=qemu-nox \
		AUXV6_TEST_SCRIPT=tools/tests/termcheck-smoke.cmds

test-termcheck-full: aux.bootkern $(EXT2IMG)
	@$(MAKE) qemu-guesttest-template \
		AUXV6_QEMU_TARGET=qemu-nox \
		AUXV6_TEST_SCRIPT=tools/tests/termcheck-full.cmds

test-terminal-regression: aux.bootkern $(EXT2IMG)
	@$(MAKE) test-termcheck-full
	@$(MAKE) test-termdemo-smoke
	@$(MAKE) test-termcap-smoke

test-termdemo-smoke: aux.bootkern $(EXT2IMG)
	@$(MAKE) qemu-guesttest-template \
		AUXV6_QEMU_TARGET=qemu-nox \
		AUXV6_TEST_SCRIPT=tools/tests/termdemo-smoke.cmds

test-termcap-smoke: aux.bootkern $(EXT2IMG)
	@$(MAKE) qemu-guesttest-template \
		AUXV6_QEMU_TARGET=qemu-nox \
		AUXV6_TEST_SCRIPT=tools/tests/termcap-smoke.cmds

# CUT HERE
# prepare dist for students
# after running make dist, probably want to
# rename it to rev0 or rev1 or so on and then
# check in that version.

EXTRA=\
	tools/mkfs.c tools/stage-fat-root.sh user/ulib.c include/user.h user/cat.c user/echo.c user/fatregress.c user/grep.c user/kill.c\
	user/stdio.c user/regex.c user/calloc.c\
	user/date.c user/time.c user/killall.c user/halt.c\
	user/lsof.c user/which.c user/file.c\
	user/id.c user/login.c user/ln.c user/ls.c user/free.c user/df.c user/ps.c user/fsregress.c user/mkdir.c user/mount.c user/mounts.c user/mounttest.c user/umount.c user/passwd.c user/pwd.c user/chmod.c user/chown.c user/chgrp.c user/rm.c user/netinfo.c user/stressfs.c user/su.c user/usertests.c user/vblktest.c user/ahcitest.c user/wc.c user/whoami.c user/zombie.c\
	user/printf.c user/umalloc.c\
	README targetfs/etc/hosts targetfs/etc/fstab targetfs/etc/profile targetfs/etc/termcap targetfs/etc/passwd targetfs/etc/hostname config/dot-bochsrc tools/*.pl tools/toc.* tools/runoff tools/runoff1 tools/runoff.list\
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

.PHONY: dist-test dist ext2-reset fat-reset ext2root qemu-ext2root qemu-nox-ext2root qemu-gdb-ext2root qemu-nox-gdb-ext2root qemu-fat qemu-nox-fat qemu-oldinit e1000
