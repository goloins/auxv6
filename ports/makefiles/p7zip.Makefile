# Makefile.auxv6 - p7zip portable first-pass lane for auxv6
#
# This lane is intentionally "non-first-class": it is for early configure/build
# validation while the libc/kernel portability surface is still being finished.
#
# Expected usage:
#   make ports-sync
#   make PORTS=1 ports-progs
# or directly:
#   make -C ports/7zip-26.00 -f Makefile.auxv6 all
#
# Output:
#   p7zip (ELF artifact used by ports-progs install logic)
#   .auxv6-build/first-pass.log (configure+build transcript)

PORTS_COMMON_CALLER := $(lastword $(MAKEFILE_LIST))
include $(dir $(abspath $(PORTS_COMMON_CALLER)))../../config/ports-common.mk

# PORTS_COMMON_CALLER is captured before the include above, so it correctly
# refers to this Makefile rather than the last file pulled in by libc.mk.
SRCDIR := $(realpath $(dir $(abspath $(PORTS_COMMON_CALLER))))
BUILDDIR := $(SRCDIR)/.auxv6-build
OUT := $(SRCDIR)/p7zip
LOG := $(BUILDDIR)/first-pass.log

TOOL_GCC_INCLUDE := $(shell $(CC) -print-file-name=include)
C7Z_DIR := $(SRCDIR)/C/Util/7z
C7Z_MK := $(C7Z_DIR)/makefile.gcc
C7Z_OUT := $(C7Z_DIR)/b/g_x86/7zdec

COMMON_CPPFLAGS := -nostdinc -I$(ROOT)/include -I$(ROOT)/include/posix -I$(ROOT)/include/posix/sys -isystem $(TOOL_GCC_INCLUDE) -D__linux__=1
COMMON_CFLAGS := -fno-pic -static -fno-builtin -fno-strict-aliasing -O2 -Wall -m32 -fno-stack-protector -std=gnu17
COMMON_LDFLAGS := -m32 -no-pie -nostdlib -static -Wl,--allow-multiple-definition $(AUXV6_CRT0_OBJ)
LIBC_FALLBACK_A := $(ROOT)/targetfs/lib/libc.a
LIBC_LINK_A := $(firstword $(wildcard $(AUXV6_LIBC_A)) $(wildcard $(LIBC_FALLBACK_A)))
LIBGCC ?= $(shell $(CC) -m32 -print-libgcc-file-name 2>/dev/null)
PORT_LIBS := $(AUXV6_AUXRT_A) $(LIBC_LINK_A) $(LIBGCC)

.PHONY: all clean first-pass check-host-contamination

all: first-pass $(OUT) check-host-contamination

first-pass: | $(BUILDDIR)
	@echo "p7zip: starting first configure/build pass" > "$(LOG)"
	@if [ ! -f "$(C7Z_MK)" ]; then \
		echo "p7zip: missing upstream build script at $(C7Z_MK)" | tee -a "$(LOG)" >&2; \
		exit 1; \
	fi
	@if [ -z "$(LIBC_LINK_A)" ]; then \
		echo "p7zip: missing libc archive; expected $(AUXV6_LIBC_A) or $(LIBC_FALLBACK_A)" | tee -a "$(LOG)" >&2; \
		exit 1; \
	fi
	@set +e; \
	cd "$(C7Z_DIR)" && \
			$(MAKE) -f makefile.gcc -j1 \
			CROSS_COMPILE="$(TOOLPREFIX)" \
			CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" STRIP="$(STRIP)" \
			MY_ARCH="-m32" USE_ASM="" \
			CFLAGS_BASE2="$(COMMON_CPPFLAGS) $(COMMON_CFLAGS)" \
			CFLAGS_WARN_WALL="-Wall -Wextra" \
			MY_LIBS="$(COMMON_LDFLAGS) $(PORT_LIBS)" \
			LIB2="" \
		>>"$(LOG)" 2>&1; \
	rc=$$?; \
	echo "p7zip: first-pass make rc=$$rc" >>"$(LOG)"; \
	if [ $$rc -ne 0 ]; then \
		echo "p7zip: first-pass portability failures recorded (non-fatal in staging lane)" >>"$(LOG)"; \
	fi
	@tail -100 "$(LOG)"

$(BUILDDIR):
	mkdir -p "$(BUILDDIR)"

$(OUT): first-pass
	@for cand in \
		"$(C7Z_OUT)" \
		"$(C7Z_DIR)/_o/7zdec"; do \
		if [ -f "$$cand" ] && $(OBJDUMP) -f "$$cand" 2>/dev/null | grep -q 'file format elf32-i386'; then \
			cp "$$cand" "$(OUT)"; \
			chmod 0755 "$(OUT)"; \
			exit 0; \
		fi; \
	done; \
	echo "p7zip: no usable ELF binary produced by upstream C utility makefile.gcc" >&2; \
	exit 1

check-host-contamination:
	@! grep -En '(^|[[:space:]])(cc|gcc|clang|i386-jos-elf-gcc)([[:space:]].*)?(-I|-isystem)[[:space:]]*(/usr/include|/usr/local/include|/opt/homebrew/include|/Library/Developer/CommandLineTools/usr/include|/Applications/Xcode.*/usr/include)' "$(LOG)" >/dev/null || \
		(echo "ERROR: host header path detected in p7zip port build log" >&2; \
		echo "Inspect $(LOG) for details." >&2; \
		exit 1)

clean:
	@set +e; \
	if [ -f "$(C7Z_MK)" ]; then \
		$(MAKE) -C "$(C7Z_DIR)" -f makefile.gcc clean >/dev/null 2>&1; \
	fi; \
	rm -rf "$(BUILDDIR)" "$(OUT)"
