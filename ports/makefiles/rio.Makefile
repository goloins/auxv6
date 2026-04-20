# Makefile.auxv6 - plan9port rio portable first-pass lane for auxv6
#
# This lane is intentionally "non-first-class": it is for early configure/build
# validation while the libc/kernel portability surface is still being finished.
#
# Expected usage:
#   make ports-sync
#   make PORTS=1 ports-progs
# or directly:
#   make -C ports/plan9port-master -f Makefile.auxv6 all
#
# Output:
#   rio (ELF artifact used by ports-progs install logic)
#   .auxv6-build/first-pass.log (configure+build transcript)

PORTS_COMMON_CALLER := $(lastword $(MAKEFILE_LIST))
include $(dir $(abspath $(PORTS_COMMON_CALLER)))../../config/ports-common.mk

# PORTS_COMMON_CALLER is captured before the include above, so it correctly
# refers to this Makefile rather than the last file pulled in by libc.mk.
SRCDIR := $(realpath $(dir $(abspath $(PORTS_COMMON_CALLER))))
BUILDDIR := $(SRCDIR)/.auxv6-build
OUT := $(SRCDIR)/rio
LOG := $(BUILDDIR)/first-pass.log

TOOL_GCC_INCLUDE := $(shell $(CC) -print-file-name=include)
RIO_DIR := $(SRCDIR)/src/cmd/rio
RIO_BIN := $(BUILDDIR)/rio
# winwatch.c and xshove.c are standalone plan9port-draw/lib9-dependent utilities, not the WM itself
RIO_SRCS := $(filter-out %/winwatch.c %/xshove.c,$(shell find $(RIO_DIR) -maxdepth 1 -type f -name '*.c' 2>/dev/null))

COMMON_CPPFLAGS := -nostdinc -I$(ROOT)/include -I$(ROOT)/include/posix -I$(ROOT)/include/posix/sys -I$(SRCDIR)/include -I$(SRCDIR)/src/lib9 -I$(SRCDIR)/src/libdraw -I$(SRCDIR)/src/libmemdraw -I$(SRCDIR)/src/libthread -isystem $(TOOL_GCC_INCLUDE) -D__linux__=1
COMMON_CFLAGS := -fno-pic -static -fno-builtin -fno-strict-aliasing -O2 -Wall -m32 -fno-stack-protector -std=gnu17 -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=unused-but-set-variable -Wno-error=parentheses -Wno-error=switch
COMMON_LDFLAGS := -m32 -no-pie -nostdlib -static -Wl,--allow-multiple-definition $(AUXV6_CRT0_OBJ)
LIBC_FALLBACK_A := $(ROOT)/targetfs/lib/libc.a
LIBC_LINK_A := $(firstword $(wildcard $(AUXV6_LIBC_A)) $(wildcard $(LIBC_FALLBACK_A)))
PORT_LIBS := $(AUXV6_AUXRT_A) $(AUXV6_X11_A) $(LIBC_LINK_A) $(LIBGCC)
TARGETFS_DIR ?= $(ROOT)/targetfs
TARGETFS_USR_BIN := $(TARGETFS_DIR)/usr/bin

.PHONY: all clean first-pass check-host-contamination install-targetfs

all: first-pass $(OUT) check-host-contamination

first-pass: | $(BUILDDIR)
	@echo "rio: starting first configure/build pass" > "$(LOG)"
	@if [ ! -d "$(RIO_DIR)" ]; then \
		echo "rio: missing source directory $(RIO_DIR)" | tee -a "$(LOG)" >&2; \
		exit 1; \
	fi
	@if [ -z "$(RIO_SRCS)" ]; then \
		echo "rio: no rio C sources found under $(RIO_DIR)" | tee -a "$(LOG)" >&2; \
		exit 1; \
	fi
	@if [ -z "$(LIBC_LINK_A)" ]; then \
		echo "rio: missing libc archive; expected $(AUXV6_LIBC_A) or $(LIBC_FALLBACK_A)" | tee -a "$(LOG)" >&2; \
		exit 1; \
	fi
	@set +e; \
	rc=0; \
	objs=""; \
	mkdir -p "$(BUILDDIR)/rio-objs"; \
	for src in $(RIO_SRCS); do \
		base="$$(basename "$$src" .c)"; \
		obj="$(BUILDDIR)/rio-objs/$$base.o"; \
		if ! $(CC) $(COMMON_CPPFLAGS) $(COMMON_CFLAGS) -c "$$src" -o "$$obj" >>"$(LOG)" 2>&1; then \
			rc=1; \
		fi; \
		objs="$$objs $$obj"; \
	done; \
	if [ $$rc -eq 0 ]; then \
		$(CC) $(COMMON_LDFLAGS) $$objs $(PORT_LIBS) -o "$(RIO_BIN)" >>"$(LOG)" 2>&1 || rc=$$?; \
	fi; \
	echo "rio: first-pass make rc=$$rc" >>"$(LOG)"; \
	if [ $$rc -ne 0 ]; then \
		echo "rio: first-pass portability failures recorded (non-fatal in staging lane)" >>"$(LOG)"; \
	fi
	@tail -100 "$(LOG)"

$(BUILDDIR):
	mkdir -p "$(BUILDDIR)"

$(OUT): first-pass
	@for cand in "$(RIO_BIN)" "$(SRCDIR)/rio" "$(RIO_DIR)/rio"; do \
		if [ -f "$$cand" ] && $(OBJDUMP) -f "$$cand" 2>/dev/null | grep -q 'file format elf32-i386'; then \
			cp "$$cand" "$(OUT)"; \
			chmod 0755 "$(OUT)"; \
			exit 0; \
		fi; \
	done; \
	echo "rio: no usable ELF binary produced" >&2; \
	exit 1

check-host-contamination:
	@! grep -En '(^|[[:space:]])(cc|gcc|clang|i386-jos-elf-gcc)([[:space:]].*)?(-I|-isystem)[[:space:]]*(/usr/include|/usr/local/include|/opt/homebrew/include|/Library/Developer/CommandLineTools/usr/include|/Applications/Xcode.*/usr/include)' "$(LOG)" >/dev/null || \
		(echo "ERROR: host header path detected in rio port build log" >&2; \
		echo "Inspect $(LOG) for details." >&2; \
		exit 1)

install-targetfs: $(OUT)
	@install -d "$(TARGETFS_USR_BIN)"
	@install -m 0755 "$(OUT)" "$(TARGETFS_USR_BIN)/rio"

clean:
	rm -rf "$(BUILDDIR)" "$(OUT)"
