OBJS = \
	kernel/core/blockdev.o\
	kernel/fs/bio.o\
	kernel/driver/console.o\
	kernel/core/exec.o\
	kernel/fs/file.o\
	kernel/fs/fs.o\
	kernel/fs/vfs.o\
	kernel/fs/vfs_xv6fs.o\
	kernel/fs/procfs.o\
	kernel/fs/vfs_ext2.o\
	kernel/driver/ide.o\
	kernel/driver/ioapic.o\
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
	kernel/net/socket.o\
	kernel/net/device.o\
	kernel/net/route.o\
	kernel/net/loopback.o\
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

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

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

initcode: kernel/boot/initcode.S
	$(CC) $(CFLAGS) -nostdinc -Iinclude -c kernel/boot/initcode.S -o initcode.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o initcode.out initcode.o
	$(OBJCOPY) -S -O binary initcode.out initcode
	$(OBJDUMP) -S initcode.o > initcode.asm

aux.kern: $(OBJS) kernel/core/entry.o entryother initcode config/kernel.ld
	$(LD) $(LDFLAGS) -T config/kernel.ld -o aux.kern kernel/core/entry.o $(OBJS) -b binary initcode entryother
	$(OBJDUMP) -S aux.kern > kernel.asm
	$(OBJDUMP) -t aux.kern | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > kernel.sym

# kernelmemfs is a copy of kernel that maintains the
# disk image in memory instead of writing to a disk.
# This is not so useful for testing persistent storage or
# exploring disk buffering implementations, but it is
# great for testing the kernel on real hardware without
# needing a scratch disk.
MEMFSOBJS = $(filter-out kernel/driver/ide.o,$(OBJS)) kernel/driver/memide.o
kernelmemfs: $(MEMFSOBJS) kernel/core/entry.o entryother initcode config/kernel.ld fs.img
	$(LD) $(LDFLAGS) -T config/kernel.ld -o kernelmemfs kernel/core/entry.o  $(MEMFSOBJS) -b binary initcode entryother fs.img
	$(OBJDUMP) -S kernelmemfs > kernelmemfs.asm
	$(OBJDUMP) -t kernelmemfs | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > kernelmemfs.sym

tags: $(OBJS) kernel/boot/entryother.S user/_init
	etags kernel/**/*.S kernel/**/*.c user/*.c

kernel/core/vectors.S: tools/vectors.pl
	./tools/vectors.pl > kernel/core/vectors.S

ULIB = user/ulib.o user/usys.o user/printf.o user/umalloc.o

# sh is close to xv6 MAXFILE; compile with -Os to keep the binary under limit.
user/sh.o: user/sh.c
	$(CC) $(CFLAGS) -Os -c -o $@ $<

# usertests is also close to xv6 MAXFILE once shared userland grows.
user/usertests.o: user/usertests.c
	$(CC) $(CFLAGS) -Os -c -o $@ $<

user/%: user/%.o $(ULIB)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^
	$(OBJDUMP) -S $@ > $(basename $@).asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(basename $@).sym

_cat: user/cat
	cp user/cat _cat

_echo: user/echo
	cp user/echo _echo

_forktest: user/forktest
	cp user/forktest _forktest

_fsregress: user/fsregress
	cp user/fsregress _fsregress

_grep: user/grep
	cp user/grep _grep

_init: user/init
	cp user/init _init

_id: user/id
	cp user/id _id

_kill: user/kill
	cp user/kill _kill

_login: user/login
	cp user/login _login

_ln: user/ln
	cp user/ln _ln

_ls: user/ls
	cp user/ls _ls

_lsblk: user/lsblk
	cp user/lsblk _lsblk

_mkdir: user/mkdir
	cp user/mkdir _mkdir

_mounts: user/mounts
	cp user/mounts _mounts

_mounttest: user/mounttest
	cp user/mounttest _mounttest

_mount: user/mount
	cp user/mount _mount

_umount: user/umount
	cp user/umount _umount

_uname: user/uname
	cp user/uname _uname

_pwd: user/pwd
	cp user/pwd _pwd

_rm: user/rm
	cp user/rm _rm

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

_netinfo: user/netinfo
	cp user/netinfo _netinfo

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

_usertests: user/usertests
	cp user/usertests _usertests

_wc: user/wc
	cp user/wc _wc

_zombie: user/zombie
	cp user/zombie _zombie

user/forktest: user/forktest.o user/ulib.o user/usys.o
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o user/forktest user/forktest.o user/ulib.o user/usys.o
	$(OBJDUMP) -S user/forktest > forktest.asm

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
	_forktest\
	_fsregress\
	_grep\
	_init\
	_id\
	_kill\
	_login\
	_ln\
	_ls\
	_lsblk\
	_mkdir\
	_mount\
	_mounts\
	_mounttest\
	_umount\
	_uname\
	_pwd\
	_rm\
	_sh\
	_sockettest\
	_su\
	_whoami\
	_tcptest\
	_ping\
	_netinfo\
	_passwd\
	_chmod\
	_chown\
	_chgrp\
	_stressfs\
	_usertests\
	_wc\
	_zombie\

fs.img: mkfs README etc.hosts etc.fstab etc.profile etc.passwd etc.groups etc.hostname $(UPROGS)
	./mkfs fs.img README etc.hosts etc.fstab etc.profile etc.passwd etc.groups etc.hostname $(UPROGS)

-include kernel/**/*.d
-include user/*.d
-include **/*.d

clean: 
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*.o *.d *.asm *.sym kernel/core/vectors.S bootblock entryother \
	initcode initcode.out aux.kern xv6.img fs.img kernelmemfs \
	xv6memfs.img mkfs .gdbinit \
	test_ext2.img \
	$(UPROGS) \
	.ext2root \
	kernel/**/*.o kernel/**/*.d kernel/**/*.asm \
	user/*.o user/*.d user/*.asm user/cat user/echo user/forktest \
	user/fsregress \
	user/grep user/id user/init user/kill user/ln user/ls user/lsblk user/mkdir \
	user/mount user/mounts user/mounttest user/umount \
	user/uname \
	user/passwd user/pwd user/chmod user/chown user/chgrp user/rm user/sh user/sockettest user/su user/whoami user/tcptest user/ping user/netinfo user/stressfs user/usertests user/wc user/zombie user/login

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
# qemu* targets already depend on $(EXT2IMG), so always attach it as index=2.
EXT2QEMU = -drive file=$(EXT2IMG)$(comma)index=2$(comma)media=disk$(comma)format=raw
QEMUOPTS = -drive file=fs.img,index=1,media=disk,format=raw -drive file=xv6.img,index=0,media=disk,format=raw $(EXT2QEMU) -smp $(CPUS) -m 512 $(QEMUEXTRA)

test_ext2.img:
	rm -rf .ext2root
	mkdir -p .ext2root
	printf "hello world\n" > .ext2root/hello.txt
	genext2fs -b 2048 -d .ext2root test_ext2.img

qemu: fs.img xv6.img $(EXT2IMG)
	$(QEMU) -serial mon:stdio $(QEMUOPTS)

qemu-memfs: xv6memfs.img
	$(QEMU) -drive file=xv6memfs.img,index=0,media=disk,format=raw -smp $(CPUS) -m 256

qemu-nox: fs.img xv6.img $(EXT2IMG)
	$(QEMU) -nographic $(QEMUOPTS)

.gdbinit: config/.gdbinit.tmpl
	sed "s/localhost:1234/localhost:$(GDBPORT)/" < $^ > $@

qemu-gdb: fs.img xv6.img $(EXT2IMG) .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -serial mon:stdio $(QEMUOPTS) -S $(QEMUGDB)

qemu-nox-gdb: fs.img xv6.img $(EXT2IMG) .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -nographic $(QEMUOPTS) -S $(QEMUGDB)

# CUT HERE
# prepare dist for students
# after running make dist, probably want to
# rename it to rev0 or rev1 or so on and then
# check in that version.

EXTRA=\
	tools/mkfs.c user/ulib.c include/user.h user/cat.c user/echo.c user/forktest.c user/grep.c user/kill.c\
	user/id.c user/login.c user/ln.c user/ls.c user/fsregress.c user/mkdir.c user/mount.c user/mounts.c user/mounttest.c user/umount.c user/passwd.c user/pwd.c user/chmod.c user/chown.c user/chgrp.c user/rm.c user/netinfo.c user/stressfs.c user/su.c user/usertests.c user/wc.c user/whoami.c user/zombie.c\
	user/printf.c user/umalloc.c\
	README etc.hosts etc.fstab etc.profile etc.passwd etc.hostname config/dot-bochsrc tools/*.pl tools/toc.* tools/runoff tools/runoff1 tools/runoff.list\
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

.PHONY: dist-test dist
