OBJS = \
	kernel/core/blockdev.o\
	kernel/fs/bio.o\
	kernel/driver/console.o\
	kernel/driver/tty_ldisc.o\
	kernel/driver/pty.o\
	kernel/driver/tuntap.o\
	kernel/audio/audio_core.o\
	kernel/driver/audio_pci.o\
	kernel/driver/audio_pci_common.o\
	kernel/driver/audio_intel_ac97.o\
	kernel/driver/audio_realtek_ac97.o\
	kernel/driver/audio_creative_live.o\
	kernel/driver/audio_creative_audigy.o\
	kernel/driver/audio_cmedia_cm8738.o\
	kernel/driver/audio_via_envy24.o\
	kernel/driver/audio_yamaha_dsxg.o\
	kernel/driver/audio_ess_maestro.o\
	kernel/driver/audio_adi_soundmax.o\
	kernel/driver/audio_sigmatel_hda.o\
	kernel/driver/audio_intel_hda.o\
	kernel/driver/audio_realtek_hda.o\
	kernel/driver/audio_conexant_hda.o\
	kernel/driver/audio_nvidia_mcp.o\
	kernel/driver/audio_creative_xfi.o\
	kernel/core/exec.o\
	kernel/fs/file.o\
	kernel/fs/fs.o\
	kernel/fs/vfs.o\
	kernel/fs/procfs.o\
	kernel/fs/vfs_ext2.o\
	kernel/fs/vfs_msdosfs.o\
	kernel/fs/vfs_exfat.o\
	kernel/fs/vfs_btrfs.o\
	kernel/fs/vfs_ufs2.o\
	kernel/fs/vfs_isofs.o\
	kernel/fs/vfs_tmpfs.o\
	kernel/fs/vfs_nfs.o\
	kernel/driver/ide.o\
	kernel/driver/ioapic.o\
	kernel/driver/pci.o\
	kernel/driver/modem.o\
	kernel/driver/firewire.o\
	kernel/driver/wifi.o\
	kernel/driver/ieee802154.o\
	kernel/driver/rtl815x.o\
	kernel/driver/usb.o\
	kernel/driver/usb_uhci.o\
	kernel/driver/usb_ohci.o\
	kernel/driver/usb_ehci.o\
	kernel/driver/usb_xhci.o\
	kernel/driver/conexant_hsf.o\
	kernel/driver/agere_lt.o\
	kernel/driver/smartlink.o\
	kernel/driver/pctel.o\
	kernel/driver/intel_softmodem.o\
	kernel/driver/motorola_sm56.o\
	kernel/driver/dma.o\
	kernel/driver/virtio.o\
	kernel/driver/virtio_blk.o\
	kernel/driver/virtio_gpu.o\
	kernel/driver/intel_gfx.o\
	kernel/driver/virtio_net.o\
	kernel/driver/ahci.o\
	kernel/driver/nvme.o\
	kernel/driver/e1000.o\
	kernel/driver/i219.o\
	kernel/driver/i226.o\
	kernel/driver/ax88179_pci.o\
	kernel/driver/pcnet.o\
	kernel/driver/rtl8111.o\
	kernel/driver/rtl8125.o\
	kernel/driver/rtl8139.o\
	kernel/driver/tg3.o\
	kernel/driver/bnxt.o\
	kernel/driver/atlantic.o\
	kernel/driver/skge.o\
	kernel/driver/via_rhine.o\
	kernel/driver/igb.o\
	kernel/driver/ixgbe.o\
	kernel/driver/i40e.o\
	kernel/driver/ice.o\
	kernel/driver/bnx2.o\
	kernel/driver/bnx2x.o\
	kernel/driver/mlx4_en.o\
	kernel/driver/mlx5e.o\
	kernel/driver/ena.o\
	kernel/driver/alx.o\
	kernel/driver/nforce.o\
	kernel/driver/vmxnet3.o\
	kernel/driver/netvsc.o\
	kernel/driver/loop.o\
	kernel/core/kalloc.o\
	kernel/core/pagedb.o\
	kernel/vm/buddy.o\
	kernel/vm/pagealloc.o\
	kernel/vm/fault.o\
	kernel/vm/vma.o\
	kernel/core/kmalloc.o\
	kernel/driver/kbd.o\
	kernel/driver/mouse.o\
	kernel/driver/lapic.o\
	kernel/fs/log.o\
	kernel/core/main.o\
	kernel/driver/mp.o\
	kernel/core/multiboot.o\
	kernel/driver/picirq.o\
	kernel/core/irq.o\
	kernel/core/pipe.o\
	kernel/core/proc.o\
	kernel/core/proc_fdscan.o\
	kernel/core/proc_lifecycle.o\
	kernel/core/proc_signal.o\
	kernel/core/proc_sched.o\
	kernel/core/proc_stats.o\
	kernel/core/sleeplock.o\
	kernel/core/spinlock.o\
	kernel/core/segreload.o\
	kernel/core/libgcc_compat.o\
	kernel/core/string.o\
	kernel/core/swtch.o\
	kernel/core/syscall.o\
	kernel/core/ktime.o\
	kernel/core/rng.o\
	kernel/core/sysfile.o\
	kernel/core/sysproc.o\
	kernel/core/trap.o\
	kernel/core/trap_fault.o\
	kernel/core/trap_diag.o\
	kernel/core/trapasm.o\
	kernel/driver/serial.o\
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

# Keep deprecated xv6fs backend available only when explicitly requested.
LEGACY_XV6FS ?= 0

ifeq ($(LEGACY_XV6FS),1)
OBJS += kernel/fs/vfs_xv6fs.o
endif

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
AR = $(TOOLPREFIX)ar
RANLIB = $(TOOLPREFIX)ranlib
STRIP = $(TOOLPREFIX)strip
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
	@$(OBJDUMP) -f "$(LIBGCC)" 2>/dev/null | grep -q 'file format elf32-i386' || \
		(echo "***" 1>&2; \
		echo "*** Error: selected LIBGCC is not an elf32-i386 archive/object set." 1>&2; \
		echo "*** LIBGCC=$(LIBGCC)" 1>&2; \
		echo "*** A host x86_64 archive may have been selected accidentally." 1>&2; \
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
	printf '/* generated by Makefile */\n#ifndef AUXV6_ROOTFS_CONFIG_H\n#define AUXV6_ROOTFS_CONFIG_H\n#define ROOTFS_TYPE_XV6FS 1\n#define ROOTFS_TYPE_EXT2 2\n#define CONFIG_LEGACY_XV6FS %s\n#define ROOTFS_TYPE %s\n#define ROOTFS_DEV %s\n#endif\n' \
	  "$(LEGACY_XV6FS)" "$(ROOTFS_TYPE_VALUE)" "$(ROOTFS_DEV_VALUE)" > "$$tmp"; \
	if ! cmp -s "$$tmp" "$@" 2>/dev/null; then mv "$$tmp" "$@"; else rm -f "$$tmp"; fi

$(EXTRA_CFLAGS_STAMP): FORCE Makefile
	@tmp="$@.tmp"; \
	printf '%s\n' "$(EXTRA_CFLAGS)" > "$$tmp"; \
	if ! cmp -s "$$tmp" "$@" 2>/dev/null; then mv "$$tmp" "$@"; else rm -f "$$tmp"; fi

$(OBJS) kernel/core/entry.o: $(ROOTFS_CONFIG) $(EXTRA_CFLAGS_STAMP)

user/%.o: $(ROOTFS_CONFIG) $(EXTRA_CFLAGS_STAMP)

libc/%.o: $(ROOTFS_CONFIG) $(EXTRA_CFLAGS_STAMP)

aux.bootkern: bootblock aux.kern
	dd if=/dev/zero of=aux.bootkern count=10000
	dd if=bootblock of=aux.bootkern conv=notrunc
	dd if=aux.kern of=aux.bootkern seek=1 conv=notrunc

ifeq ($(LEGACY_XV6FS),1)
xv6memfs.img: bootblock kernelmemfs
	dd if=/dev/zero of=xv6memfs.img count=10000
	dd if=bootblock of=xv6memfs.img conv=notrunc
	dd if=kernelmemfs of=xv6memfs.img seek=1 conv=notrunc
else
xv6memfs.img:
	@echo "xv6memfs is deprecated and disabled by default; set LEGACY_XV6FS=1 to enable." >&2
	@false
endif

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
	$(LD) $(LDFLAGS) -T config/kernel.ld -o aux.kern kernel/core/entry.o $(OBJS) -b binary entryother $(LIBGCC)
	@$(OBJDUMP) -f aux.kern | grep -q 'file format elf32-i386' || \
		(echo "ERROR: aux.kern is not elf32-i386." 1>&2; exit 1)
	@set -e; \
	line="$$($(TOOLPREFIX)size aux.kern | tail -n 1)"; \
	set -- $$line; \
	text=$${1:-0}; data=$${2:-0}; bss=$${3:-0}; dec=$${4:-0}; \
	hard_total=$$((8*1024*1024)); \
	soft_total=$$((6*1024*1024)); \
	bss_hard=$$((4*1024*1024)); \
	printf "kernel-size: text=%s data=%s bss=%s total=%s bytes\n" "$$text" "$$data" "$$bss" "$$dec"; \
	if [ "$$dec" -gt "$$hard_total" ]; then \
		echo "ERROR: kernel total size exceeds hard budget (8MB)." >&2; \
		exit 1; \
	fi; \
	if [ "$$bss" -gt "$$bss_hard" ]; then \
		echo "ERROR: kernel .bss exceeds hard budget (4MB)." >&2; \
		exit 1; \
	fi; \
	if [ "$$dec" -gt "$$soft_total" ]; then \
		echo "WARN: kernel total size exceeds soft budget (6MB); consider reducing static footprint or expanding early-map budget intentionally." >&2; \
	fi
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
	$(LD) $(LDFLAGS) -T config/kernel.ld -o kernelmemfs kernel/core/entry.o  $(MEMFSOBJS) -b binary entryother fs.img $(LIBGCC)
	$(OBJDUMP) -S kernelmemfs > kernelmemfs.asm
	$(OBJDUMP) -t kernelmemfs | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > kernelmemfs.sym

tags: $(OBJS) kernel/boot/entryother.S user/_init
	etags kernel/**/*.S kernel/**/*.c libc/*.[cS] user/*.c

kernel/core/vectors.S: tools/vectors.pl
	./tools/vectors.pl > kernel/core/vectors.S

LIBC_OBJS = libc/ulib.o libc/string.o libc/errstr.o libc/umalloc.o libc/tty.o libc/inet.o libc/fmt.o libc/dirent.o libc/fnmatch.o libc/glob.o libc/ftw.o libc/fts.o libc/locale.o libc/pwdgrp.o libc/env.o libc/conf.o libc/path.o libc/tempfile.o libc/timecore.o libc/resource.o libc/netdb.o libc/stdlib.o libc/randlib.o libc/mman.o libc/posix_fs.o libc/posix.o libc/pthread.o libc/syslog.o libc/stdio.o libc/regex.o libc/calloc.o libc/setjmp.o libc/sigjmp.o libc/math.o libc/usys.o libc/printf.o libc/resolve.o libc/crypt.o libc/utmpx.o
LIBAUXRT_OBJS = libc/libterm.o libc/checksum.o libc/gzip.o libc/bzip2.o libc/bzlib.o libc/compress.o libc/decompress.o libc/huffman.o libc/crctable.o libc/randtable.o libc/blocksort.o libc/user_font.o
CRT0_OBJ = libc/crt0.o
LIBC_A = libc/libc.a
AUXRT_A = libc/libauxrt.a
X11_OBJS = user/x11.o
X11_A = user/libX11.a

.PHONY: libc-rebuild
# Force a clean libc runtime rebuild (crt0 + libc + auxrt) and restage libc.a.
libc-rebuild:
	rm -f $(LIBC_OBJS) $(LIBAUXRT_OBJS) $(CRT0_OBJ) $(LIBC_A) $(AUXRT_A) $(TARGETFS_LIBC_A)
	$(MAKE) $(CRT0_OBJ) $(LIBC_A) $(AUXRT_A) $(TARGETFS_LIBC_A)

# Convenience static archive of the full auxv6 userland runtime library.
# Used by native programs and by ports that need to link against our libc
# without knowing about individual implementation objects.
$(LIBC_A): $(LIBC_OBJS)
	$(AR) rcs $@ $(LIBC_OBJS)
	$(RANLIB) $@

$(AUXRT_A): $(LIBAUXRT_OBJS)
	$(AR) rcs $@ $(LIBAUXRT_OBJS)
	$(RANLIB) $@

$(X11_A): $(X11_OBJS)
	$(AR) rcs $@ $(X11_OBJS)
	$(RANLIB) $@

USER_STAGE_DIR = user/.stage
USER_PROG_JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
PORTS ?= 1
PORTS_MANIFEST ?= ports/ports.list
PORTS_ENABLED_LIST ?= ports/ports-enabled.list
PORTS_SYNC_SCRIPT ?= tools/sync-ports.sh
PORTS_BUILD_LOG ?= ports/ports-build.log
PORTS_SKIP_LIST ?= dash dwm st dmenu

# sh is close to xv6 MAXFILE; compile with -Os to keep the binary under limit.
user/sh.o: user/sh.c
	$(CC) $(CFLAGS) -Os -c -o $@ $<

# usertests is also close to xv6 MAXFILE once shared userland grows.
user/usertests.o: user/usertests.c
	$(CC) $(CFLAGS) -Os -c -o $@ $<

user/%: user/%.o $(CRT0_OBJ) $(LIBC_A) $(AUXRT_A) | toolchain-check
	$(LD) $(LDFLAGS) -N -e _start -Ttext 0 -o $@ $(CRT0_OBJ) $< $(AUXRT_A) $(LIBC_A) $(LIBGCC)
	@$(OBJDUMP) -f $@ | grep -q 'file format elf32-i386' || \
		(echo "ERROR: $@ is not elf32-i386." 1>&2; exit 1)
	$(OBJDUMP) -S $@ > $(basename $@).asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(basename $@).sym

ACCOUNT_MGMT_BINS = \
	user/useradd \
	user/usermod \
	user/userdel \
	user/groupadd \
	user/groupmod \
	user/groupdel \
	user/groups

$(ACCOUNT_MGMT_BINS): user/%: user/%.o user/accountdb.o $(CRT0_OBJ) $(LIBC_A) $(AUXRT_A) | toolchain-check
	$(LD) $(LDFLAGS) -N -e _start -Ttext 0 -o $@ $(CRT0_OBJ) user/$*.o user/accountdb.o $(AUXRT_A) $(LIBC_A) $(LIBGCC)
	@$(OBJDUMP) -f $@ | grep -q 'file format elf32-i386' || \
		(echo "ERROR: $@ is not elf32-i386." 1>&2; exit 1)
	$(OBJDUMP) -S $@ > $(basename $@).asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(basename $@).sym

$(USER_STAGE_DIR):
	mkdir -p $(USER_STAGE_DIR)

$(USER_STAGE_DIR)/%: user/%.o $(CRT0_OBJ) $(LIBC_A) $(AUXRT_A) | $(USER_STAGE_DIR) toolchain-check
	$(LD) $(LDFLAGS) -N -e _start -Ttext 0 -o $@ $(CRT0_OBJ) $< $(AUXRT_A) $(LIBC_A) $(LIBGCC)
	@$(OBJDUMP) -f $@ | grep -q 'file format elf32-i386' || \
		(echo "ERROR: $@ is not elf32-i386." 1>&2; exit 1)

_cat: user/cat
	cp user/cat _cat

_more: user/more
	cp user/more _more

_less: _more
	ln -sf _more _less

_awk: user/awk
	cp user/awk _awk

_sed: user/sed
	cp user/sed _sed

_find: user/find
	cp user/find _find

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

_dd: user/dd
	cp user/dd _dd

_ddate: user/ddate
	cp user/ddate _ddate

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

_6ftp: user/6ftp
	cp user/6ftp _6ftp

_6get: user/6get
	cp user/6get _6get

_6fetch: user/6fetch
	cp user/6fetch _6fetch

_6vi: user/6vi
	cp user/6vi _6vi

_6nano: user/6nano
	cp user/6nano _6nano

_6doc: user/6doc
	cp user/6doc _6doc

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

_testdaemon: user/testdaemon
	cp user/testdaemon _testdaemon

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

_tuntapctl: user/tuntapctl
	cp user/tuntapctl _tuntapctl

_tuntest: user/tuntest
	cp user/tuntest _tuntest

_v6dhcpd: user/v6dhcpd
	cp user/v6dhcpd _v6dhcpd

_ntpd: user/ntpd
	cp user/ntpd _ntpd

_passwd: user/passwd
	cp user/passwd _passwd

_useradd: user/useradd
	cp user/useradd _useradd

_usermod: user/usermod
	cp user/usermod _usermod

_userdel: user/userdel
	cp user/userdel _userdel

_groupadd: user/groupadd
	cp user/groupadd _groupadd

_groupmod: user/groupmod
	cp user/groupmod _groupmod

_groupdel: user/groupdel
	cp user/groupdel _groupdel

_groups: user/groups
	cp user/groups _groups

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

_gfxperf: user/gfxperf
	cp user/gfxperf _gfxperf

_kallocstress: user/kallocstress
	cp user/kallocstress _kallocstress

_kernperf: user/kernperf
	cp user/kernperf _kernperf

_vmprobe: user/vmprobe
	cp user/vmprobe _vmprobe

_vmguardtest: user/vmguardtest
	cp user/vmguardtest _vmguardtest

_cowtest: user/cowtest
	cp user/cowtest _cowtest

_cowexectest: user/cowexectest
	cp user/cowexectest _cowexectest

_vmreservetest: user/vmreservetest
	cp user/vmreservetest _vmreservetest

_kmemstress: user/kmemstress
	cp user/kmemstress _kmemstress

_bcachestress: user/bcachestress
	cp user/bcachestress _bcachestress

_sigtest: user/sigtest
	cp user/sigtest _sigtest

_stackgrowtest: user/stackgrowtest
	cp user/stackgrowtest _stackgrowtest
_fdtest: user/fdtest
	cp user/fdtest _fdtest
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

_xtermprobe: user/xtermprobe
	cp user/xtermprobe _xtermprobe

_tail: user/tail
	cp user/tail _tail

_lsof: user/lsof
	cp user/lsof _lsof

_which: user/which
	cp user/which _which

_cowsay: user/cowsay
	cp user/cowsay _cowsay

_file: user/file
	cp user/file _file

_gunzip: user/gunzip
	cp user/gunzip _gunzip

_tar: user/tar
	cp user/tar _tar

_bzip2: user/bzip2
	cp user/bzip2 _bzip2

_bunzip2: user/bunzip2
	cp user/bunzip2 _bunzip2

_ar: user/ar
	cp user/ar _ar

_uniq: user/uniq
	cp user/uniq _uniq

_sort: user/sort
	cp user/sort _sort

_sum: user/sum
	cp user/sum _sum

_sleep: user/sleep
	cp user/sleep _sleep

_yes: user/yes
	cp user/yes _yes

_true: user/boolean
	cp user/boolean _true

_false: user/boolean
	cp user/boolean _false

_sync: user/sync
	cp user/sync _sync

_touch: user/touch
	cp user/touch _touch

_md5sum: user/hashsum
	cp user/hashsum _md5sum

_sha1sum: user/hashsum
	cp user/hashsum _sha1sum

_sha224sum: user/hashsum
	cp user/hashsum _sha224sum

_sha256sum: user/hashsum
	cp user/hashsum _sha256sum

_sha384sum: user/hashsum
	cp user/hashsum _sha384sum

_sha512sum: user/hashsum
	cp user/hashsum _sha512sum

_base32: user/baseenc
	cp user/baseenc _base32

_base64: user/baseenc
	cp user/baseenc _base64

_asroot: user/asroot
	cp user/asroot _asroot

_audioctl: user/audioctl
	cp user/audioctl _audioctl

_audiostat: user/audiostat
	cp user/audiostat _audiostat

_audiotest: user/audiotest
	cp user/audiotest _audiotest

_audiotone: user/audiotone
	cp user/audiotone _audiotone

_audiopollstress: user/audiopollstress
	cp user/audiopollstress _audiopollstress

_audiod: user/audiod
	cp user/audiod _audiod

_audiodctl: user/audiodctl
	cp user/audiodctl _audiodctl

_lockprobe: user/lockprobe
	cp user/lockprobe _lockprobe

_server7: user/server7
	cp user/server7 _server7

_x6: user/x6
	cp user/x6 _x6

_xinit: user/xinit
	cp user/xinit _xinit

_xwmtrace: user/xwmtrace
	cp user/xwmtrace _xwmtrace

user/xwmselftest: user/xwmselftest.o $(X11_A) $(CRT0_OBJ) $(LIBC_A) $(AUXRT_A) | toolchain-check
	$(LD) $(LDFLAGS) -N -e _start -Ttext 0 -o $@ $(CRT0_OBJ) user/xwmselftest.o $(X11_A) $(AUXRT_A) $(LIBC_A) $(LIBGCC)
	$(OBJDUMP) -S $@ > $(basename $@).asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(basename $@).sym

_xwmselftest: user/xwmselftest
	cp user/xwmselftest _xwmselftest

_startx: user/startx
	cp user/startx _startx

_x6test: user/x6test
	cp user/x6test _x6test

user/wallpaper: user/wallpaper.o user/img.o user/img_png.o user/img_jpg.o $(CRT0_OBJ) $(LIBC_A) $(AUXRT_A) | toolchain-check
	$(LD) $(LDFLAGS) -N -e _start -Ttext 0 -o $@ $(CRT0_OBJ) user/wallpaper.o user/img.o user/img_png.o user/img_jpg.o $(AUXRT_A) $(LIBC_A) $(LIBGCC)
	$(OBJDUMP) -S $@ > $(basename $@).asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(basename $@).sym

$(USER_STAGE_DIR)/wallpaper: user/wallpaper.o user/img.o user/img_png.o user/img_jpg.o $(CRT0_OBJ) $(LIBC_A) $(AUXRT_A) | $(USER_STAGE_DIR) toolchain-check
	$(LD) $(LDFLAGS) -N -e _start -Ttext 0 -o $@ $(CRT0_OBJ) user/wallpaper.o user/img.o user/img_png.o user/img_jpg.o $(AUXRT_A) $(LIBC_A) $(LIBGCC)

_wallpaper: user/wallpaper
	cp user/wallpaper _wallpaper

_date: user/date
	cp user/date _date

_time: user/time
	cp user/time _time

_dmesg: user/dmesg
	cp user/dmesg _dmesg

_dash: ports/dash-0.5.12/Makefile.auxv6 $(CRT0_OBJ) $(LIBC_A) $(AUXRT_A)
	$(MAKE) -f ports/dash-0.5.12/Makefile.auxv6 all
	cp ports/dash-0.5.12/_dash _dash

_dwm: ports/dwm-6.8/Makefile.auxv6 $(CRT0_OBJ) $(LIBC_A) $(AUXRT_A) $(X11_A)
	$(MAKE) -f ports/dwm-6.8/Makefile.auxv6 all
	cp ports/dwm-6.8/_dwm _dwm

_st: ports/st-0.9.3/Makefile.auxv6 $(CRT0_OBJ) $(LIBC_A) $(AUXRT_A) $(X11_A)
	$(MAKE) -f ports/st-0.9.3/Makefile.auxv6 all
	cp ports/st-0.9.3/_st _st

.PHONY: dmenu-port-build
dmenu-port-build: ports/dmenu-5.4/Makefile.auxv6 $(CRT0_OBJ) $(LIBC_A) $(AUXRT_A) $(X11_A)
	$(MAKE) -f ports/dmenu-5.4/Makefile.auxv6 all

_dmenu: dmenu-port-build
	cp ports/dmenu-5.4/_dmenu _dmenu

_stest: dmenu-port-build
	cp ports/dmenu-5.4/_stest _stest

_symlinktest: user/symlinktest
	cp user/symlinktest _symlinktest

_nftwtest: user/nftwtest
	cp user/nftwtest _nftwtest

_ftwtest: user/ftwtest
	cp user/ftwtest _ftwtest

_ftstest: user/ftstest
	cp user/ftstest _ftstest

.PHONY: userprogs userprogs-oldinit
userprogs:
	+$(MAKE) ports-progs PORTS=$(PORTS)
	+$(MAKE) -j$(USER_PROG_JOBS) $(UPROGS)

userprogs-oldinit:
	+$(MAKE) ports-progs PORTS=$(PORTS)
	+$(MAKE) -j$(USER_PROG_JOBS) $(UPROGS_OLDINIT)

.PHONY: strip-uprogs strip-uprogs-oldinit
strip-uprogs: userprogs
	@set -e; \
	for p in $(UPROGS); do \
		if [ -f "$$p" ] && [ ! -L "$$p" ]; then \
			$(STRIP) -g "$$p"; \
		fi; \
	done

strip-uprogs-oldinit: userprogs-oldinit
	@set -e; \
	for p in $(UPROGS_OLDINIT); do \
		if [ -f "$$p" ] && [ ! -L "$$p" ]; then \
			$(STRIP) -g "$$p"; \
		fi; \
	done

.PHONY: ports-sync ports-progs
ports-sync:
	$(PORTS_SYNC_SCRIPT) $(PORTS_MANIFEST) $(PORTS_ENABLED_LIST)

ports-progs:
	@if [ "$(PORTS)" != "1" ]; then \
		echo "ports: skipped (PORTS=$(PORTS))"; \
		exit 0; \
	fi
	@mkdir -p ports
	@: > $(PORTS_BUILD_LOG)
	@if [ ! -f "$(PORTS_MANIFEST)" ]; then \
		echo "ports: manifest $(PORTS_MANIFEST) not found; skipping" | tee -a $(PORTS_BUILD_LOG); \
		exit 0; \
	fi
	@if [ ! -f "$(PORTS_ENABLED_LIST)" ]; then \
		echo "ports: enabled-list $(PORTS_ENABLED_LIST) not found; skipping" | tee -a $(PORTS_BUILD_LOG); \
		exit 0; \
	fi
	@hooks_seen="ports/.ports-install-hooks.seen"; \
	: > "$$hooks_seen"; \
	while IFS='|' read -r name url pclass srcdir binname; do \
		case "$$name" in ''|'#'*) continue ;; esac; \
		skip=0; \
		for builtin in $(PORTS_SKIP_LIST); do \
			if [ "$$name" = "$$builtin" ]; then \
				skip=1; \
				break; \
			fi; \
		done; \
		if [ "$$skip" = "1" ]; then \
			echo "ports: skipping in-tree port $$name" >> $(PORTS_BUILD_LOG); \
			continue; \
		fi; \
		if ! sed -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$$//' "$(PORTS_ENABLED_LIST)" | grep -v '^$$' | grep -Fxq "$$name"; then \
			echo "ports: skipping disabled port $$name" >> $(PORTS_BUILD_LOG); \
			continue; \
		fi; \
		if [ -z "$$pclass" ]; then pclass=user; fi; \
		if [ -z "$$srcdir" ]; then srcdir="$$name"; fi; \
		if [ -z "$$binname" ]; then binname="$$name"; fi; \
		portdir="ports/$$srcdir"; \
		if [ ! -d "$$portdir" ]; then \
			echo "ports: source directory missing for $$name (expected $$portdir); run make ports-sync" | tee -a $(PORTS_BUILD_LOG); \
			continue; \
		fi; \
		if [ ! -f "$$portdir/Makefile.auxv6" ]; then \
			echo "ports: missing Makefile.auxv6 for $$name (expected $$portdir/Makefile.auxv6)" | tee -a $(PORTS_BUILD_LOG); \
			continue; \
		fi; \
		canon_mk="ports/makefiles/$$name.Makefile"; \
		if [ -f "$$canon_mk" ] && ! cmp -s "$$canon_mk" "$$portdir/Makefile.auxv6"; then \
			if cp "$$canon_mk" "$$portdir/Makefile.auxv6"; then \
				echo "ports: synced canonical makefile for $$name" >> $(PORTS_BUILD_LOG); \
			else \
				echo "ports: failed to sync canonical makefile for $$name from $$canon_mk" | tee -a $(PORTS_BUILD_LOG); \
				continue; \
			fi; \
		fi; \
		canon_postinstall="ports/makefiles/$$name.postinstall"; \
		if [ -f "$$canon_postinstall" ] && { [ ! -f "$$portdir/pkg-postinstall.local" ] || ! cmp -s "$$canon_postinstall" "$$portdir/pkg-postinstall.local"; }; then \
			if cp "$$canon_postinstall" "$$portdir/pkg-postinstall.local"; then \
				echo "ports: synced canonical postinstall hook for $$name" >> $(PORTS_BUILD_LOG); \
			else \
				echo "ports: failed to sync canonical postinstall hook for $$name from $$canon_postinstall" | tee -a $(PORTS_BUILD_LOG); \
				continue; \
			fi; \
		fi; \
		stamp="$$portdir/built.auxv6"; \
		legacy_stamp="$$portdir/.auxv6-built.stamp"; \
		expected_sig="$$({ \
			sha256sum "$$portdir/Makefile.auxv6"; \
			sha256sum "$(CRT0_OBJ)"; \
			sha256sum "$(AUXRT_A)"; \
			sha256sum "$(LIBC_A)"; \
		} 2>/dev/null | sha256sum | awk '{print $$1}')"; \
		stored_sig=""; \
		if [ -f "$$stamp" ]; then \
			stored_sig="$$(sed -n '1p' "$$stamp")"; \
		elif [ -f "$$legacy_stamp" ]; then \
			stored_sig="legacy-marker"; \
		fi; \
		rebuild=1; \
		if [ -n "$$expected_sig" ] && [ -n "$$stored_sig" ] && [ "$$stored_sig" = "$$expected_sig" ]; then \
			rebuild=0; \
		fi; \
		if [ "$$rebuild" = "1" ]; then \
			echo "ports: building $$name from $$portdir" | tee -a $(PORTS_BUILD_LOG); \
			if ! $(MAKE) -C "$$portdir" -f Makefile.auxv6 all; then \
				echo "ports: build failed for $$name" | tee -a $(PORTS_BUILD_LOG); \
				continue; \
			fi; \
			echo "$$expected_sig" > "$$stamp"; \
			touch "$$legacy_stamp"; \
		else \
			echo "ports: up-to-date $$name (skipping build)" | tee -a $(PORTS_BUILD_LOG); \
		fi; \
		portbin=""; \
		for cand in "$$portdir/_$$name" "$$portdir/$$binname"; do \
			if [ -f "$$cand" ] && $(OBJDUMP) -f "$$cand" 2>/dev/null | grep -q 'file format elf32-i386'; then \
				portbin="$$cand"; \
				break; \
			fi; \
		done; \
		if [ -z "$$portbin" ]; then \
			echo "ports: built binary not found for $$name (looked for _$$name and $$binname)" | tee -a $(PORTS_BUILD_LOG); \
			continue; \
		fi; \
		if ! $(OBJDUMP) -f "$$portbin" 2>/dev/null | grep -q 'file format elf32-i386'; then \
			echo "ports: refusing non-ELF artifact for $$name: $$portbin" | tee -a $(PORTS_BUILD_LOG); \
			continue; \
		fi; \
		case "$$pclass" in \
			system|helper) destdir="$(TARGETFS_DIR)/bin" ;; \
			user) destdir="$(TARGETFS_DIR)/usr/bin" ;; \
			sbin) destdir="$(TARGETFS_DIR)/usr/sbin" ;; \
			*) echo "ports: invalid class '$$pclass' for $$name (use system/helper/user/sbin)" | tee -a $(PORTS_BUILD_LOG); continue ;; \
		esac; \
		install -d "$$destdir"; \
		install -m 0755 "$$portbin" "$$destdir/$$binname"; \
		echo "ports: installed $$name -> $$destdir/$$binname" | tee -a $(PORTS_BUILD_LOG); \
		hook_key="$$name|$$srcdir"; \
		if ! grep -Fxq "$$hook_key" "$$hooks_seen"; then \
			if grep -Eq '^[[:space:]]*install-targetfs:' "$$portdir/Makefile.auxv6"; then \
				if ! $(MAKE) -C "$$portdir" -f Makefile.auxv6 install-targetfs; then \
					echo "ports: install-targetfs hook failed for $$name" | tee -a $(PORTS_BUILD_LOG); \
					continue; \
				fi; \
			fi; \
			postinstall_local="$(CURDIR)/$$portdir/pkg-postinstall.local"; \
			if [ -s "$$postinstall_local" ]; then \
				postinstall_tmp="$(CURDIR)/$$portdir/.auxv6-postinstall"; \
				rm -rf "$$postinstall_tmp"; \
				mkdir -p "$$postinstall_tmp"; \
				if ! (cd "$$postinstall_tmp" && /bin/sh "$$postinstall_local"); then \
					echo "ports: failed to generate postinstall for $$name from $$postinstall_local" | tee -a $(PORTS_BUILD_LOG); \
					continue; \
				fi; \
				if [ -f "$$postinstall_tmp/postinstall" ]; then \
					echo "ports: running postinstall hook for $$name" | tee -a $(PORTS_BUILD_LOG); \
					if ! (cd "$$postinstall_tmp" && PKG_INSTALL_ROOT="$(TARGETFS_DIR)" /bin/sh ./postinstall >> "$(CURDIR)/$(PORTS_BUILD_LOG)" 2>&1); then \
						echo "ports: postinstall hook failed for $$name" | tee -a $(PORTS_BUILD_LOG); \
						continue; \
					fi; \
				else \
					echo "ports: postinstall hook missing generated script for $$name" | tee -a $(PORTS_BUILD_LOG); \
					continue; \
				fi; \
			fi; \
			echo "$$hook_key" >> "$$hooks_seen"; \
		fi; \
	done < "$(PORTS_MANIFEST)"; \
	rm -f "$$hooks_seen"

ifeq ($(LEGACY_XV6FS),1)
mkfs: tools/mkfs.c include/fs.h
	gcc -Werror -Wall -o mkfs tools/mkfs.c
else
mkfs:
	@echo "xv6 mkfs is deprecated and disabled by default; set LEGACY_XV6FS=1 to enable." >&2
	@false
endif

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: %.o

UPROGS=\
	_cat\
	_more\
	_less\
	_awk\
	_sed\
	_find\
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
	_dd\
	_ddate\
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
	_6ftp\
	_6get\
	_6vi\
	_6nano\
	_6fetch\
	_6doc\
	_telnet\
	_runlevel\
	_telinit\
	_route\
	_arp\
	_rarp\
	_ip\
	_tuntapctl\
	_tuntest\
	_v6dhcpd\
	_ntpd\
	_passwd\
	_useradd\
	_usermod\
	_userdel\
	_groupadd\
	_groupmod\
	_groupdel\
	_groups\
	_chmod\
	_chown\
	_chgrp\
	_chvt\
	_stressfs\
	_schedperf\
	_fsperf\
	_gfxperf\
	_kallocstress\
	_kernperf\
	_vmprobe\
	_vmguardtest\
	_cowtest\
	_cowexectest\
	_vmreservetest\
	_kmemstress\
	_bcachestress\
	_sigtest\
	_stackgrowtest\
	_fdtest\
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
	_xtermprobe\
	_tail\
	_lsof\
	_which\
	_cowsay\
	_file\
	_gunzip\
	_tar\
	_bzip2\
	_bunzip2\
	_ar\
	_uniq\
	_sort\
	_sum\
	_sleep\
	_yes\
	_true\
	_false\
	_sync\
	_touch\
	_md5sum\
	_sha1sum\
	_sha224sum\
	_sha256sum\
	_sha384sum\
	_sha512sum\
	_base32\
	_base64\
	_asroot\
	_audioctl\
	_audiostat\
	_audiotest\
	_audiotone\
	_audiopollstress\
	_audiod\
	_audiodctl\
	_lockprobe\
	_date\
	_time\
	_dmesg\
	_server7\
	_x6\
	_xinit\
	_xwmtrace\
	_xwmselftest\
	_startx\
	_x6test\
	_wallpaper\
	_dash\
	_symlinktest\
	_nftwtest\
	_ftwtest\
	_ftstest\
	_testdaemon\

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

# Single-image GRUB boot targets.
# Build this target with ROOTFS_DEV set to /dev/hda1 (HD_PART_DEV(0,1)).
auxv6.img: ROOTFS_DEV_VALUE=4
auxv6.img: tools/build-auxv6-img.sh tools/stage-ext2-root.sh aux.kern strip-uprogs \
		$(ROOTFS_COMMON_FILES) $(ROOTFS_RC_FILES) $(ROOTFS_MAN_FILES) \
		$(ROOTFS_TARGETFS_FILES)
	sh tools/stage-ext2-root.sh .auxv6root .auxv6-part.img \
		$(ROOTFS_COMMON_FILES) $(ROOTFS_RC_FILES) $(ROOTFS_MAN_FILES) \
		$(ROOTFS_TARGETFS_FILES) $(UPROGS)
	sh tools/build-auxv6-img.sh .auxv6root $@

qemu-singleimage: auxv6.img
	$(QEMU) -serial mon:stdio -drive file=auxv6.img,index=0,media=disk,format=raw $(QEMUNETOPTS_E1000) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-singleimage: auxv6.img
	$(QEMU) -nographic -drive file=auxv6.img,index=0,media=disk,format=raw -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-gdb-singleimage: .gdbinit auxv6.img
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -serial mon:stdio -drive file=auxv6.img,index=0,media=disk,format=raw -smp $(CPUS) -m 512 -S $(QEMUGDB) $(QEMUEXTRA)

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
	auxv6.img \
	.auxv6-part.img \
	vblk0.img \
	vblk1.img \
	vblk-stress.img \
	ahci-stress.img \
	nvme-ext2.img \
	nvme-fat.img \
	nvme-fat32.img \
	nvme-exfat.img \
	nvme-btrfs.img \
	nvme-ufs2.img \
	$(UPROGS) \
	$(UPROGS_OLDINIT) \
	.ext2root \
	.ext2root-oldinit \
	.ext2root-server7 \
	.auxv6root \
	.fatroot \
	.fat32root \
	.exfatroot \
	.btrfsroot \
	.ufs2root \
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
	user/tuntapctl user/tuntest \
	user/dhcp user/v6dhcpd user/ntpd user/nslookup \
	user/6ftp \
	user/6get \
	user/6nano \
	user/6fetch \
	user/6doc \
	user/abrowse \
	user/lsof user/which user/file user/ar user/bzip2 user/bunzip2 \
	user/uniq user/sort user/sum user/sleep user/yes user/boolean user/sync user/touch user/hashsum user/baseenc user/asroot \
	user/audioctl user/audiostat user/audiotest user/audiotone user/audiopollstress user/audiod user/audiodctl \
	user/server7 user/xwmselftest \
	user/top \
	user/date user/ddate user/time user/killall user/halt user/wallpaper \
	user/passwd user/pwd user/chmod user/chown user/chgrp user/rm user/reset user/clear user/sh user/sigtest user/stackgrowtest user/sockettest user/su user/whoami user/tcptest user/ping user/netinfo user/stressfs user/usertests user/wc user/zombie user/login user/getty user/chvt user/termdemo user/termcheck user/dmesg user/tail user/lspci user/v6init user/testdaemon \
	user/useradd user/usermod user/userdel user/groupadd user/groupmod user/groupdel user/groups \
	user/cowsay \
	user/schedperf user/fsperf user/gfxperf user/kallocstress user/kernperf user/bcachestress user/kmemstress \
	libc/libc.a \
	ports/*/built.auxv6 ports/*/.auxv6-built.stamp ports/*/.auxv6-postinstall/postinstall ports/*/.auxv6-postinstall

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
TARGETFS_LIB ?= $(TARGETFS_DIR)/lib
TARGETFS_LIBC_A ?= $(TARGETFS_LIB)/libc.a
TARGETFS_INCLUDE ?= $(TARGETFS_DIR)/usr/include
TARGETFS_MAN_DIR ?= $(TARGETFS_DIR)/usr/share/man
EXT2ROOT_FSTAB ?= $(TARGETFS_ETC)/fstab.ext2root
INCLUDE_HEADER_SOURCES = $(shell find include -type f 2>/dev/null)
TARGETFS_INCLUDE_HEADERS = $(patsubst include/%,$(TARGETFS_INCLUDE)/%,$(INCLUDE_HEADER_SOURCES))
ROOTFS_LEGACY_FILES =
ifeq ($(LEGACY_XV6FS),1)
ROOTFS_LEGACY_FILES += $(TARGETFS_SBIN)/mount.xv6fs
endif
ROOTFS_COMMON_FILES = README $(TARGETFS_ETC)/hosts $(EXT2ROOT_FSTAB) $(TARGETFS_ETC)/profile $(TARGETFS_ETC)/termcap $(TARGETFS_ETC)/passwd $(TARGETFS_ETC)/group $(TARGETFS_ETC)/hostname $(TARGETFS_ETC)/motd $(TARGETFS_ETC)/resolv.conf $(TARGETFS_SBIN)/mount.ext2 $(TARGETFS_SBIN)/mount.msdosfs $(TARGETFS_SBIN)/mount.exfat $(TARGETFS_SBIN)/mount.isofs $(TARGETFS_LIBC_A) $(TARGETFS_INCLUDE_HEADERS) $(ROOTFS_LEGACY_FILES) $(TARGETFS_DIR)/tmp/test.iso
ROOTFS_RC_FILES = $(TARGETFS_ETC)/rc.S $(TARGETFS_ETC)/rc.0 $(TARGETFS_ETC)/rc.1 $(TARGETFS_ETC)/rc.2 $(TARGETFS_ETC)/rc.3 $(TARGETFS_ETC)/rc.6
ROOTFS_RC_FILES_SERVER7 = $(filter-out $(TARGETFS_ETC)/rc.2,$(ROOTFS_RC_FILES)) $(TARGETFS_ETC)/rc.2.server7
ROOTFS_MAN_FILES = $(wildcard $(TARGETFS_MAN_DIR)/*.md)
ROOTFS_TARGETFS_FILES = $(shell find $(TARGETFS_DIR) -type f -o -type l 2>/dev/null)
FATIMG ?= test_fat.img
FATROOT_STAGE ?= .fatroot
FAT32IMG ?= nvme-fat32.img
FAT32ROOT_STAGE ?= .fat32root
EXFATIMG ?= nvme-exfat.img
EXFATROOT_STAGE ?= .exfatroot
BTRFSIMG ?= nvme-btrfs.img
BTRFSROOT_STAGE ?= .btrfsroot
UFS2IMG ?= nvme-ufs2.img
UFS2ROOT_STAGE ?= .ufs2root
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

$(TARGETFS_LIBC_A): $(LIBC_A)
	install -d $(TARGETFS_LIB)
	install -m 0644 $(LIBC_A) $@

$(TARGETFS_INCLUDE)/%: include/%
	install -d $(dir $@)
	install -m 0644 $< $@

#nice
test_ext2.img: tools/stage-ext2-root.sh $(ROOTFS_COMMON_FILES) $(ROOTFS_RC_FILES) $(ROOTFS_MAN_FILES) $(ROOTFS_TARGETFS_FILES) strip-uprogs
	sh tools/stage-ext2-root.sh .ext2root $(EXT2IMG) $(ROOTFS_COMMON_FILES) $(ROOTFS_RC_FILES) $(ROOTFS_MAN_FILES) $(ROOTFS_TARGETFS_FILES) $(UPROGS)

test_ext2_server7.img: tools/stage-ext2-root.sh $(ROOTFS_COMMON_FILES) $(ROOTFS_RC_FILES_SERVER7) $(ROOTFS_MAN_FILES) $(ROOTFS_TARGETFS_FILES) strip-uprogs
	sh tools/stage-ext2-root.sh .ext2root-server7 test_ext2_server7.img $(ROOTFS_COMMON_FILES) $(ROOTFS_RC_FILES_SERVER7) $(ROOTFS_MAN_FILES) $(ROOTFS_TARGETFS_FILES) $(UPROGS)

test_ext2_oldinit.img: tools/stage-ext2-root.sh $(ROOTFS_COMMON_FILES) strip-uprogs-oldinit
	sh tools/stage-ext2-root.sh .ext2root-oldinit test_ext2_oldinit.img $(ROOTFS_COMMON_FILES) $(UPROGS_OLDINIT)

test_fat.img: tools/stage-fat-root.sh
	sh tools/stage-fat-root.sh $(FATROOT_STAGE) $(FATIMG)

ext2-reset:
	rm -f $(EXT2IMG)
	$(MAKE) $(EXT2IMG)

fat-reset:
	rm -f $(FATIMG)
	$(MAKE) $(FATIMG)

btrfs-reset:
	rm -f $(BTRFSIMG)
	$(MAKE) $(BTRFSIMG)

ufs2-reset:
	rm -f $(UFS2IMG)
	$(MAKE) $(UFS2IMG)

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

# NVMe FAT32 test image: 128 MB FAT32 volume for NVMe + msdosfs FAT32 driver validation.
# Includes both short (8.3) and long filename entries.
# Inside the guest:
#   mkdir /mnt/nvme && mount -t msdosfs n0 /mnt/nvme
nvme-fat32.img: tools/stage-fat32-root.sh
	sh tools/stage-fat32-root.sh $(FAT32ROOT_STAGE) $(FAT32IMG)

# NVMe exFAT test image: 128 MB exFAT volume for NVMe + exfat driver validation.
nvme-exfat.img: tools/stage-exfat-root.sh
	sh tools/stage-exfat-root.sh $(EXFATROOT_STAGE) $(EXFATIMG)

fat32-reset:
	rm -f $(FAT32IMG)
	$(MAKE) $(FAT32IMG)

exfat-reset:
	rm -f $(EXFATIMG)
	$(MAKE) $(EXFATIMG)

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

# NVMe Btrfs test image: 64 MB Btrfs volume for NVMe + btrfs driver validation.
# Linux-host only (mkfs.btrfs from btrfs-progs).
# Inside the guest:
#   mkdir /mnt/nvme && mount -t btrfs n0 /mnt/nvme
nvme-btrfs.img: tools/stage-btrfs-root.sh
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "error: building $(BTRFSIMG) requires root (mkfs.btrfs test image flow)" >&2; \
		echo "hint: run 'sudo make $(BTRFSIMG)' or 'sudo make test-btrfs-smoke'" >&2; \
		exit 1; \
	fi
	sh tools/stage-btrfs-root.sh $(BTRFSROOT_STAGE) $(BTRFSIMG)

# NVMe UFS2 test image scaffold.
# We currently do not ship an in-tree UFS2 image builder.
# Use a prebuilt UFS2 image at $(UFS2IMG) for now.
nvme-ufs2.img:
	@echo "error: no in-tree UFS2 image builder yet for $@" >&2; \
	echo "hint: place a prebuilt UFS2 image at $(UFS2IMG) and re-run your qemu target" >&2; \
	exit 1

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

ifeq ($(LEGACY_XV6FS),1)
qemu-memfs: xv6memfs.img
	$(QEMU) -drive file=xv6memfs.img,index=0,media=disk,format=raw $(QEMUNETOPTS) -smp $(CPUS) -m 256
else
qemu-memfs:
	@echo "qemu-memfs is deprecated and disabled by default; set LEGACY_XV6FS=1 to enable." >&2
	@false
endif

qemu-nox: aux.bootkern $(EXT2IMG)
	$(QEMU) -nographic -drive file=aux.bootkern,index=0,media=disk,format=raw -drive file=$(EXT2IMG),index=2,media=disk,format=raw $(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

# Audio-enabled QEMU boot path using AC97 (supported by in-tree intel-ac97 probe).
# Run guest-side flow: audiotone -> audiodctl track-loop.
qemu-audiotest: aux.bootkern $(EXT2IMG)
	QEMU_AUDIO_DRV=coreaudio $(QEMU) -serial mon:stdio \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-audiodev coreaudio,id=snd0 -device AC97,audiodev=snd0 \
		$(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-audiotest: aux.bootkern $(EXT2IMG)
	QEMU_AUDIO_DRV=coreaudio $(QEMU) -nographic \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-audiodev coreaudio,id=snd0 -device AC97,audiodev=snd0 \
		$(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

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

# NVMe FAT32 test: 128 MB FAT32 volume via NVMe.
# Inside the guest:
#   mkdir /mnt/nvme && mount -t msdosfs n0 /mnt/nvme
qemu-nvme-fat32: aux.bootkern $(EXT2IMG) nvme-fat32.img
	$(QEMU) -serial mon:stdio \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=nvme-fat32.img,if=none,id=nvme0,format=raw \
		-device nvme,drive=nvme0,serial=auxv6nvme0 \
		$(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-nvme-fat32: aux.bootkern $(EXT2IMG) nvme-fat32.img
	$(QEMU) -nographic \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=nvme-fat32.img,if=none,id=nvme0,format=raw \
		-device nvme,drive=nvme0,serial=auxv6nvme0 \
		$(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

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

# NVMe exFAT test: same config with an exFAT volume.
# Inside the guest:
#   mkdir /mnt/exfat && mount -t exfat n0 /mnt/exfat
qemu-nvme-exfat: aux.bootkern $(EXT2IMG) $(EXFATIMG)
	$(QEMU) -serial mon:stdio \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=$(EXFATIMG),if=none,id=nvme0,format=raw \
		-device nvme,drive=nvme0,serial=auxv6nvme0 \
		$(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-nvme-exfat: aux.bootkern $(EXT2IMG) $(EXFATIMG)
	$(QEMU) -nographic \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=$(EXFATIMG),if=none,id=nvme0,format=raw \
		-device nvme,drive=nvme0,serial=auxv6nvme0 \
		$(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

# NVMe Btrfs test: same config with a Btrfs test volume.
# Inside the guest:
#   mkdir /mnt/nvme && mount -t btrfs n0 /mnt/nvme
qemu-nvme-btrfs: aux.bootkern $(EXT2IMG) nvme-btrfs.img
	$(QEMU) -serial mon:stdio \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=nvme-btrfs.img,if=none,id=nvme0,format=raw \
		-device nvme,drive=nvme0,serial=auxv6nvme0 \
		$(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-nvme-btrfs: aux.bootkern $(EXT2IMG) nvme-btrfs.img
	$(QEMU) -nographic \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=nvme-btrfs.img,if=none,id=nvme0,format=raw \
		-device nvme,drive=nvme0,serial=auxv6nvme0 \
		$(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

# Btrfs host-side sanity check without launching the guest.
# Prints key superblock fields from the staged image.
btrfs-host-verify: $(BTRFSIMG)
	@BTRFS_INSPECT=$$(command -v btrfs 2>/dev/null || true); \
	if [ -z "$$BTRFS_INSPECT" ]; then \
		echo "error: btrfs command not found; install btrfs-progs" >&2; \
		exit 1; \
	fi; \
	"$$BTRFS_INSPECT" inspect-internal dump-super -f $(BTRFSIMG) | \
		grep -E 'label|num_devices|sectorsize|nodesize|sys_chunk_array_size' | head -n 8

# Build-only smoke target for manual guest-side validation flow.
# We intentionally keep execution manual in the guest console.
test-btrfs-smoke: aux.bootkern $(EXT2IMG) $(BTRFSIMG)
	@echo "Btrfs smoke artifacts are ready."
	@echo "1) Boot: make qemu-nvme-btrfs"
	@echo "2) In guest console, run:"
	@echo "   mkdir -p /mnt/nvme"
	@echo "   mount -t btrfs n0 /mnt/nvme"
	@echo "   ls -la /mnt/nvme"
	@echo "   cat /mnt/nvme/README.TXT"
	@echo "   cat /mnt/nvme/README.LNK"
	@echo "   cat /mnt/nvme/SUBDIR/NOTE.TXT"
	@echo "   echo x > /mnt/nvme/NEWFILE.TXT"

test-btrfs-regression: test-btrfs-smoke btrfs-host-verify
	@echo "Btrfs regression prep complete (host verify + guest smoke commands printed)."

# NVMe UFS2 test: same config with a UFS2 test volume.
# Inside the guest:
#   mkdir /mnt/nvme && mount -t ufs2 n0 /mnt/nvme
qemu-nvme-ufs2: aux.bootkern $(EXT2IMG) $(UFS2IMG)
	$(QEMU) -serial mon:stdio \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=$(UFS2IMG),if=none,id=nvme0,format=raw \
		-device nvme,drive=nvme0,serial=auxv6nvme0 \
		$(QEMUNETOPTS) $(QEMUGFXOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu-nox-nvme-ufs2: aux.bootkern $(EXT2IMG) $(UFS2IMG)
	$(QEMU) -nographic \
		-drive file=aux.bootkern,index=0,media=disk,format=raw \
		-drive file=$(EXT2IMG),index=2,media=disk,format=raw \
		-drive file=$(UFS2IMG),if=none,id=nvme0,format=raw \
		-device nvme,drive=nvme0,serial=auxv6nvme0 \
		$(QEMUNETOPTS) -smp $(CPUS) -m 512 $(QEMUEXTRA)

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
	tools/mkfs.c tools/stage-fat-root.sh tools/stage-exfat-root.sh tools/stage-btrfs-root.sh libc/ulib.c include/user.h user/cat.c user/echo.c user/fatregress.c user/grep.c user/kill.c\
	libc/stdio.c libc/regex.c libc/calloc.c\
	user/date.c user/time.c user/killall.c user/halt.c\
	user/lsof.c user/which.c user/file.c\
	user/id.c user/login.c user/ln.c user/ls.c user/free.c user/df.c user/ps.c user/fsregress.c user/mkdir.c user/mount.c user/mounts.c user/mounttest.c user/umount.c user/passwd.c user/useradd.c user/usermod.c user/userdel.c user/groupadd.c user/groupmod.c user/groupdel.c user/groups.c user/accountdb.c user/pwd.c user/chmod.c user/chown.c user/chgrp.c user/rm.c user/netinfo.c user/stressfs.c user/su.c user/usertests.c user/vblktest.c user/ahcitest.c user/wc.c user/whoami.c user/zombie.c\
	libc/printf.c libc/umalloc.c\
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

.PHONY: dist-test dist ext2-reset fat-reset fat32-reset exfat-reset btrfs-reset ufs2-reset ext2root qemu-ext2root qemu-nox-ext2root qemu-gdb-ext2root qemu-nox-gdb-ext2root qemu-fat qemu-nox-fat qemu-oldinit e1000 qemu-nvme-btrfs qemu-nox-nvme-btrfs qemu-nvme-ufs2 qemu-nox-nvme-ufs2 qemu-nvme-fat32 qemu-nox-nvme-fat32 qemu-nvme-exfat qemu-nox-nvme-exfat btrfs-host-verify test-btrfs-smoke test-btrfs-regression
