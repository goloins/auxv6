# Makefile.auxv6 - lacc portable first-pass lane for auxv6
#
# This lane is intentionally "non-first-class": it is for early configure/build
# validation while the libc/kernel portability surface is still being finished.
#
# Expected usage:
#   make ports-sync
#   make PORTS=1 ports-progs
# or directly:
#   make -C ports/lacc-master -f Makefile.auxv6 all
#
# Output:
#   lacc (ELF artifact used by ports-progs install logic)
#   .auxv6-build/first-pass.log (configure+build transcript)

PORTS_COMMON_CALLER := $(lastword $(MAKEFILE_LIST))
include $(dir $(abspath $(PORTS_COMMON_CALLER)))../../config/ports-common.mk

# PORTS_COMMON_CALLER is captured before the include above, so it correctly
# refers to this Makefile rather than the last file pulled in by libc.mk.
SRCDIR := $(realpath $(dir $(abspath $(PORTS_COMMON_CALLER))))
BUILDDIR := $(SRCDIR)/.auxv6-build
OUT := $(SRCDIR)/lacc
LOG := $(BUILDDIR)/first-pass.log

TOOL_GCC_INCLUDE := $(shell $(CC) -print-file-name=include)

COMMON_CPPFLAGS := -nostdinc -I$(ROOT)/include -I$(ROOT)/include/posix -I$(ROOT)/include/posix/sys -isystem $(TOOL_GCC_INCLUDE)
COMMON_CFLAGS := -fno-pic -static -fno-builtin -fno-strict-aliasing -O2 -Wall -m32 -fno-stack-protector -std=gnu17
COMMON_LDFLAGS := -m32 -no-pie -nostdlib -static -Wl,--allow-multiple-definition $(AUXV6_CRT0_OBJ)
LIBC_FALLBACK_A := $(ROOT)/targetfs/lib/libc.a
LIBC_LINK_A := $(firstword $(wildcard $(AUXV6_LIBC_A)) $(wildcard $(LIBC_FALLBACK_A)))
LIBGCC ?= $(shell $(CC) -m32 -print-libgcc-file-name 2>/dev/null)
PORT_LIBS := $(AUXV6_AUXRT_A) $(LIBC_LINK_A) $(LIBGCC)

.PHONY: all clean first-pass check-host-contamination

all: first-pass $(OUT) check-host-contamination

first-pass: | $(BUILDDIR)
	@echo "lacc: starting first configure/build pass" > "$(LOG)"
	@cd "$(SRCDIR)" && \
		env \
			CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" STRIP="$(STRIP)" \
			CPPFLAGS="$(COMMON_CPPFLAGS)" CFLAGS="$(COMMON_CFLAGS)" \
			./configure --host=i386-jos-elf >>"$(LOG)" 2>&1
	@if [ -z "$(LIBC_LINK_A)" ]; then \
		echo "lacc: missing libc archive; expected $(AUXV6_LIBC_A) or $(LIBC_FALLBACK_A)" | tee -a "$(LOG)" >&2; \
		exit 1; \
	fi
	@set +e; \
	mkdir -p "$(SRCDIR)/bin"; \
	$(CC) $(COMMON_CPPFLAGS) $(COMMON_CFLAGS) \
		-I"$(SRCDIR)/include" -include "$(SRCDIR)/config.h" -DAMALGAMATION \
		"$(SRCDIR)/src/lacc.c" \
		-o "$(SRCDIR)/bin/lacc" \
		$(COMMON_LDFLAGS) $(PORT_LIBS) >>"$(LOG)" 2>&1; \
	rc=$$?; \
	echo "lacc: first-pass make rc=$$rc" >>"$(LOG)"; \
	if [ $$rc -ne 0 ]; then \
		echo "lacc: first-pass portability failures recorded (non-fatal in staging lane)" >>"$(LOG)"; \
	fi
	@tail -100 "$(LOG)"

$(BUILDDIR):
	mkdir -p "$(BUILDDIR)"

$(OUT): first-pass
	@if [ -f "$(SRCDIR)/bin/lacc" ] && $(OBJDUMP) -f "$(SRCDIR)/bin/lacc" 2>/dev/null | grep -q 'file format elf32-i386'; then \
		cp "$(SRCDIR)/bin/lacc" "$(OUT)"; \
		chmod 0755 "$(OUT)"; \
	else \
		echo "lacc: no usable ELF binary at $(SRCDIR)/bin/lacc" >&2; \
		exit 1; \
	fi

check-host-contamination:
	@! grep -En '(^|[[:space:]])(cc|gcc|clang|i386-jos-elf-gcc)([[:space:]].*)?(-I|-isystem)[[:space:]]*(/usr/include|/usr/local/include|/opt/homebrew/include|/Library/Developer/CommandLineTools/usr/include|/Applications/Xcode.*/usr/include)' "$(LOG)" >/dev/null || \
		(echo "ERROR: host header path detected in lacc port build log" >&2; \
		echo "Inspect $(LOG) for details." >&2; \
		exit 1)

clean:
	rm -rf "$(BUILDDIR)" "$(OUT)" "$(SRCDIR)/bin" "$(SRCDIR)/config.h" "$(SRCDIR)/config.mak"
