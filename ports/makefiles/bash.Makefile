# Makefile.auxv6 - Bash portable first-pass lane for auxv6
#
# This lane is intentionally "non-first-class": it is for early configure/build
# validation while the libc/kernel portability surface is still being finished.
#
# Expected usage:
#   make ports-sync
#   make PORTS=1 ports-progs
# or directly:
#   make -C ports/bash-5.2.37 -f Makefile.auxv6 all
#
# Output:
#   _bash (small launcher stub used by ports-progs install logic)
#   .auxv6-build/first-pass.log (configure+build transcript)

ROOT ?= $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST))))../..)
SRCDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
BUILDDIR := $(SRCDIR)/.auxv6-build
OUT := $(SRCDIR)/_bash
LOG := $(BUILDDIR)/first-pass.log

CROSS_ROOT ?= /opt/cross
CROSS_BINDIR ?= $(CROSS_ROOT)/bin

ifndef TOOLPREFIX
TOOLPREFIX := $(shell \
  if i386-jos-elf-objdump -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
  then echo 'i386-jos-elf-'; \
  elif test -x '$(CROSS_BINDIR)/i386-jos-elf-objdump' && \
       '$(CROSS_BINDIR)/i386-jos-elf-objdump' -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
  then echo '$(CROSS_BINDIR)/i386-jos-elf-'; \
  elif test -x '$(CROSS_BINDIR)/i386-elf-objdump' && \
       '$(CROSS_BINDIR)/i386-elf-objdump' -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
  then echo '$(CROSS_BINDIR)/i386-elf-'; \
  elif test -x '$(CROSS_BINDIR)/i686-elf-objdump' && \
       '$(CROSS_BINDIR)/i686-elf-objdump' -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
  then echo '$(CROSS_BINDIR)/i686-elf-'; \
  elif objdump -i 2>&1 | grep 'elf32-i386' >/dev/null 2>&1; \
  then echo ''; \
  else echo "ERROR: no i386 cross-toolchain found" >&2; exit 1; fi)
endif

CC := $(TOOLPREFIX)gcc
AR := $(TOOLPREFIX)ar
RANLIB := $(TOOLPREFIX)ranlib
STRIP := $(TOOLPREFIX)strip

TOOL_GCC_INCLUDE := $(shell $(CC) -print-file-name=include)

COMMON_CPPFLAGS := -I$(ROOT)/include -I$(ROOT)/include/posix -I$(ROOT)/include/posix/sys
COMMON_CFLAGS := -fno-pic -static -fno-builtin -fno-strict-aliasing -O2 -Wall -m32 -fno-stack-protector
COMMON_LDFLAGS := -static
BUILD_CC := cc
BUILD_CFLAGS := -g -DCROSS_COMPILING -DHAVE_STRERROR=1 -DHAVE_DECL_SYS_NERR=1 -DHAVE_DECL_SYS_ERRLIST=1
CONFIGURE_CPPFLAGS := $(COMMON_CPPFLAGS) -isystem $(TOOL_GCC_INCLUDE) -D__linux__=1 \
	-DHAVE_DPRINTF=1 -DHAVE_GETHOSTNAME=1 -DHAVE_GETTIMEOFDAY=1 -DHAVE_ISBLANK=1 \
	-DPARAMS\(protos\)=protos
# Configure links probe executables; avoid crt0/libc requirements for cross checks.
CONFIGURE_LDFLAGS := -nostdlib -nostartfiles

.PHONY: all clean first-pass check-host-contamination

all: first-pass $(OUT) check-host-contamination

first-pass: | $(BUILDDIR)
	@echo "bash: starting first configure/build pass" > "$(LOG)"
	@cd "$(BUILDDIR)" && \
		env \
			CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" STRIP="$(STRIP)" \
			CPPFLAGS="$(CONFIGURE_CPPFLAGS)" CFLAGS="$(COMMON_CFLAGS)" LDFLAGS="$(CONFIGURE_LDFLAGS)" \
			../configure \
				--host=i386-jos-elf \
				--build=$$(../support/config.guess 2>/dev/null || ../config.guess 2>/dev/null || echo x86_64-unknown-linux-gnu) \
				--prefix=/usr \
				--without-bash-malloc \
				--disable-nls \
				>>"$(LOG)" 2>&1
	@cd "$(BUILDDIR)" && \
		perl -0pi -e 's@/\* #undef HAVE_DPRINTF \*/@#define HAVE_DPRINTF 1@g; s@/\* #undef HAVE_GETHOSTNAME \*/@#define HAVE_GETHOSTNAME 1@g; s@/\* #undef HAVE_GETTIMEOFDAY \*/@#define HAVE_GETTIMEOFDAY 1@g; s@/\* #undef HAVE_ISBLANK \*/@#define HAVE_ISBLANK 1@g;' config.h
	# Avoid top-level "all" because it forces host-side doc helpers (man2html).
	@set +e; \
	$(MAKE) -C "$(BUILDDIR)" -k -j1 \
		CC_FOR_BUILD="$(BUILD_CC)" CFLAGS_FOR_BUILD="$(BUILD_CFLAGS)" \
		bash >>"$(LOG)" 2>&1; \
	rc=$$?; \
	echo "bash: first-pass make rc=$$rc" >>"$(LOG)"; \
	if [ $$rc -ne 0 ]; then \
		echo "bash: first-pass portability failures recorded (non-fatal in staging lane)" >>"$(LOG)"; \
	fi
	@tail -40 "$(LOG)"

check-host-contamination:
	@! grep -En '/usr/include|/usr/local/include|/opt/homebrew/include|/Library/Developer/CommandLineTools/usr/include|/Applications/Xcode.*/usr/include' "$(LOG)" >/dev/null || \
		(echo "ERROR: host header path detected in Bash port build log" >&2; \
		echo "Inspect $(LOG) for details." >&2; \
		exit 1)

$(BUILDDIR):
	mkdir -p "$(BUILDDIR)"

$(OUT):
	@printf '#!/bin/dash\n' > "$(OUT)"
	@printf 'echo "bash port lane: configure/build pass artifact only"\n' >> "$(OUT)"
	@printf 'echo "see: %s"\n' "$(LOG)" >> "$(OUT)"
	chmod 0755 "$(OUT)"

clean:
	rm -rf "$(BUILDDIR)" "$(OUT)"
