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
#   bash (ELF artifact used by ports-progs install logic)
#   .auxv6-build/first-pass.log (configure+build transcript)

PORTS_COMMON_CALLER := $(lastword $(MAKEFILE_LIST))
include $(dir $(abspath $(PORTS_COMMON_CALLER)))../../config/ports-common.mk

SRCDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
BUILDDIR := $(SRCDIR)/.auxv6-build
LEGACY_OUT := $(SRCDIR)/_bash
OUT := $(SRCDIR)/bash
LOG := $(BUILDDIR)/first-pass.log

TOOL_GCC_INCLUDE := $(shell $(CC) -print-file-name=include)

COMMON_CPPFLAGS := -I$(ROOT)/include -I$(ROOT)/include/posix -I$(ROOT)/include/posix/sys
COMMON_CFLAGS := -fno-pic -static -fno-builtin -fno-strict-aliasing -O2 -Wall -m32 -fno-stack-protector
COMMON_LDFLAGS := -static
BUILD_CC := cc
BUILD_CFLAGS := -g -DCROSS_COMPILING -DHAVE_STRERROR=1 -DHAVE_DECL_SYS_NERR=1 -DHAVE_DECL_SYS_ERRLIST=1
CONFIGURE_CPPFLAGS := $(COMMON_CPPFLAGS) -isystem $(TOOL_GCC_INCLUDE) -D__linux__=1 \
	-DHAVE_DPRINTF=1 -DHAVE_GETHOSTNAME=1 -DHAVE_GETTIMEOFDAY=1 -DHAVE_ISBLANK=1 \
	-DPARAMS\(protos\)=protos -Dshell_input_line_property=shell_input_line \
	-DTIOCSTART=0x541C -DTIOCSTOP=0x541D
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
		perl -0pi -e 's@/\* #undef HAVE_DPRINTF \*/@#define HAVE_DPRINTF 1@g; \
		s@/\* #undef HAVE_GETHOSTNAME \*/@#define HAVE_GETHOSTNAME 1@g; \
		s@/\* #undef HAVE_GETTIMEOFDAY \*/@#define HAVE_GETTIMEOFDAY 1@g; \
		s@/\* #undef HAVE_ISBLANK \*/@#define HAVE_ISBLANK 1@g; \
		s@/\* #undef HAVE_TIMES \*/@#define HAVE_TIMES 1@g; \
		s@/\* #undef HAVE_TCGETATTR \*/@#define HAVE_TCGETATTR 1@g; \
		s@/\* #undef HAVE_KILLPG \*/@#define HAVE_KILLPG 1@g; \
		s@/\* #undef HAVE_STRPBRK \*/@#define HAVE_STRPBRK 1@g; \
		s@/\* #undef HAVE_STRTOD \*/@#define HAVE_STRTOD 1@g; \
		s@/\* #undef HAVE_STRTOLL \*/@#define HAVE_STRTOLL 1@g; \
		s@/\* #undef HAVE_STRTOUL \*/@#define HAVE_STRTOUL 1@g; \
		s@/\* #undef HAVE_STRTOULL \*/@#define HAVE_STRTOULL 1@g; \
		s@/\* #undef HAVE_STRTOIMAX \*/@#define HAVE_STRTOIMAX 1@g; \
		s@/\* #undef HAVE_STRTOUMAX \*/@#define HAVE_STRTOUMAX 1@g; \
		s@/\* #undef HAVE_STRFTIME \*/@#define HAVE_STRFTIME 1@g; \
		s@/\* #undef HAVE_POSIX_SIGNALS \*/@#define HAVE_POSIX_SIGNALS 1@g; \
		s@/\* #undef HAVE_STRCHR \*/@#define HAVE_STRCHR 1@g; \
		s@/\* #undef HAVE_BCOPY \*/@#define HAVE_BCOPY 1@g; \
		s@/\* #undef HAVE_DUP2 \*/@#define HAVE_DUP2 1@g; \
		s@/\* #undef HAVE_MKFIFO \*/@#define HAVE_MKFIFO 1@g; \
		s@/\* #undef HAVE_SELECT \*/@#define HAVE_SELECT 1@g; \
		s@/\* #undef HAVE_LONG_LONG_INT \*/@#define HAVE_LONG_LONG_INT 1@g; \
		s@/\* #undef HAVE_UNSIGNED_LONG_LONG_INT \*/@#define HAVE_UNSIGNED_LONG_LONG_INT 1@g; \
		s@/\* #undef HAVE_TZSET \*/@#define HAVE_TZSET 1@g; \
		s@/\* #undef HAVE_TZNAME \*/@#define HAVE_TZNAME 1@g; \
		s@/\* #undef HAVE_SETREUID \*/@#define HAVE_SETREUID 1@g; \
		s@/\* #undef HAVE_SETREGID \*/@#define HAVE_SETREGID 1@g; \
		s@/\* #undef HAVE_STRTOLD \*/@#define HAVE_STRTOLD 1@g; \
		s@/\* #undef HAVE_PUTCHAR \*/@#define HAVE_PUTCHAR 1@g; \
		s@#define HAVE_SYS_RANDOM_H 1@/* #undef HAVE_SYS_RANDOM_H */@g;' config.h
	# Avoid top-level "all" because it forces host-side doc helpers (man2html).
	@set +e; \
	$(MAKE) -C "$(BUILDDIR)" -j1 \
		CC_FOR_BUILD="$(BUILD_CC)" CFLAGS_FOR_BUILD="$(BUILD_CFLAGS)" \
		LDFLAGS="-nostdlib -static -Wl,--allow-multiple-definition $(AUXV6_CRT0_OBJ)" \
		LOCAL_LIBS="$(AUXV6_AUXRT_A) $(AUXV6_LIBC_A) $(LIBGCC)" \
		bash >>"$(LOG)" 2>&1; \
	rc=$$?; \
	echo "bash: first-pass make rc=$$rc" >>"$(LOG)"; \
	if [ $$rc -ne 0 ]; then \
		echo "bash: first-pass portability failures recorded (non-fatal in staging lane)" >>"$(LOG)"; \
	fi
	@tail -40 "$(LOG)"

check-host-contamination:
	@! grep -En '(^|[[:space:]])(cc|gcc|clang|i386-jos-elf-gcc)([[:space:]].*)?(-I|-isystem)[[:space:]]*(/usr/include|/usr/local/include|/opt/homebrew/include|/Library/Developer/CommandLineTools/usr/include|/Applications/Xcode.*/usr/include)' "$(LOG)" >/dev/null || \
		(echo "ERROR: host header path detected in Bash port build log" >&2; \
		echo "Inspect $(LOG) for details." >&2; \
		exit 1)

$(BUILDDIR):
	mkdir -p "$(BUILDDIR)"

$(OUT):
	@if [ -f "$(BUILDDIR)/bash" ] && $(OBJDUMP) -f "$(BUILDDIR)/bash" 2>/dev/null | grep -q 'file format elf32-i386'; then \
		cp "$(BUILDDIR)/bash" "$(OUT)"; \
		chmod 0755 "$(OUT)"; \
		rm -f "$(LEGACY_OUT)"; \
	else \
		echo "bash: no usable ELF binary at $(BUILDDIR)/bash" >&2; \
		exit 1; \
	fi

clean:
	rm -rf "$(BUILDDIR)" "$(OUT)" "$(LEGACY_OUT)"
