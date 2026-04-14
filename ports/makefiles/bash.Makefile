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

# PORTS_COMMON_CALLER is captured before the include above, so it correctly
# refers to this Makefile rather than the last file pulled in by libc.mk.
SRCDIR := $(realpath $(dir $(abspath $(PORTS_COMMON_CALLER))))
BUILDDIR := $(SRCDIR)/.auxv6-build
LEGACY_OUT := $(SRCDIR)/_bash
OUT := $(SRCDIR)/bash
LOG := $(BUILDDIR)/first-pass.log

TOOL_GCC_INCLUDE := $(shell $(CC) -print-file-name=include)

COMMON_CPPFLAGS := -nostdinc -I$(ROOT)/include -I$(ROOT)/include/posix -I$(ROOT)/include/posix/sys
COMMON_CFLAGS := -fno-pic -static -fno-builtin -fno-strict-aliasing -O2 -Wall -m32 -fno-stack-protector -std=gnu17 -Wno-error=implicit-function-declaration -Wno-error=implicit-int
COMMON_LDFLAGS := -static
BUILD_CC := cc
BUILD_CFLAGS := -g -std=gnu17 -Wno-error=implicit-function-declaration -Wno-error=implicit-int -DCROSS_COMPILING -DHAVE_STRERROR=1 -DHAVE_DECL_SYS_NERR=1 -DHAVE_DECL_SYS_ERRLIST=1
LIBC_FALLBACK_A := $(ROOT)/targetfs/lib/libc.a
LIBC_LINK_A := $(firstword $(wildcard $(AUXV6_LIBC_A)) $(wildcard $(LIBC_FALLBACK_A)))
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
			bash_cv_posix_signals=yes bash_cv_signal_vintage=posix \
			ac_cv_type_long_long_int=yes ac_cv_type_unsigned_long_long_int=yes \
			ac_cv_func_getrusage=yes \
			ac_cv_func_tcgetattr=yes \
			ac_cv_func_strchr=yes ac_cv_func_strrchr=yes ac_cv_func_bcopy=yes \
			ac_cv_func_killpg=yes ac_cv_func_mkfifo=yes bash_cv_sys_named_pipes=present \
			CPPFLAGS="$(CONFIGURE_CPPFLAGS)" CFLAGS="$(COMMON_CFLAGS)" LDFLAGS="$(CONFIGURE_LDFLAGS)" \
			../configure \
				--host=i386-jos-elf \
				--build=$$(../support/config.guess 2>/dev/null || ../config.guess 2>/dev/null || echo x86_64-unknown-linux-gnu) \
				--prefix=/usr \
				--without-bash-malloc \
				--disable-readline \
				--disable-history \
				--disable-bang-history \
				--disable-nls \
				>>"$(LOG)" 2>&1
	@set +e; \
	$(MAKE) -C "$(ROOT)" libc/libc.a libc/libauxrt.a >>"$(LOG)" 2>&1; \
	libc_rc=$$?; \
	if [ $$libc_rc -ne 0 ]; then \
		echo "bash: failed to rebuild auxv6 libc/libauxrt (rc=$$libc_rc)" | tee -a "$(LOG)" >&2; \
		exit $$libc_rc; \
	fi
	@cd "$(BUILDDIR)" && \
		perl -0pi -e 's@/\* #undef HAVE_DPRINTF \*/@#define HAVE_DPRINTF 1@g; \
		s@/\* #undef HAVE_LONG_LONG_INT \*/@#define HAVE_LONG_LONG_INT 1@g; \
		s@/\* #undef HAVE_UNSIGNED_LONG_LONG_INT \*/@#define HAVE_UNSIGNED_LONG_LONG_INT 1@g; \
		s@/\* #undef HAVE_SELECT \*/@#define HAVE_SELECT 1@g; \
		s@/\* #undef HAVE_GETHOSTNAME \*/@#define HAVE_GETHOSTNAME 1@g; \
		s@/\* #undef HAVE_GETRUSAGE \*/@#define HAVE_GETRUSAGE 1@g; \
		s@/\* #undef HAVE_GETTIMEOFDAY \*/@#define HAVE_GETTIMEOFDAY 1@g; \
		s@/\* #undef HAVE_ISBLANK \*/@#define HAVE_ISBLANK 1@g; \
		s@/\* #undef HAVE_STRCHR \*/@#define HAVE_STRCHR 1@g; \
		s@/\* #undef HAVE_STRRCHR \*/@#define HAVE_STRRCHR 1@g; \
		s@/\* #undef HAVE_BCOPY \*/@#define HAVE_BCOPY 1@g; \
		s@/\* #undef HAVE_KILLPG \*/@#define HAVE_KILLPG 1@g; \
		s@/\* #undef HAVE_MKFIFO \*/@#define HAVE_MKFIFO 1@g; \
		s@/\* #undef HAVE_POSIX_SIGNALS \*/@#define HAVE_POSIX_SIGNALS 1@g; \
		s@#define HAVE_BSD_SIGNALS 1@/* #undef HAVE_BSD_SIGNALS */@g; \
		s@/\* #undef HAVE_TCGETATTR \*/@#define HAVE_TCGETATTR 1@g; \
		s@/\* #undef HAVE_STRFTIME \*/@#define HAVE_STRFTIME 1@g; \
		s@#define HAVE_ULIMIT_H 1@/* #undef HAVE_ULIMIT_H */@g; \
		s@#define HAVE_SYS_RANDOM_H 1@/* #undef HAVE_SYS_RANDOM_H */@g;' config.h
	# Avoid top-level "all" because it forces host-side doc helpers (man2html).
	@if [ -z "$(LIBC_LINK_A)" ]; then \
		echo "bash: missing libc archive; expected $(AUXV6_LIBC_A) or $(LIBC_FALLBACK_A)" | tee -a "$(LOG)" >&2; \
		exit 1; \
	fi
	@set +e; \
	$(MAKE) -C "$(BUILDDIR)" -j1 \
		CC_FOR_BUILD="$(BUILD_CC)" CFLAGS_FOR_BUILD="$(BUILD_CFLAGS)" \
		LDFLAGS="-m32 -no-pie -nostdlib -static -Wl,--allow-multiple-definition $(AUXV6_CRT0_OBJ)" \
		LOCAL_LIBS="$(AUXV6_AUXRT_A) $(LIBC_LINK_A) $(LIBGCC)" \
		bash >>"$(LOG)" 2>&1; \
	rc=$$?; \
	echo "bash: first-pass make rc=$$rc" >>"$(LOG)"; \
	if [ $$rc -ne 0 ]; then \
		echo "bash: first-pass portability failures recorded (non-fatal in staging lane)" >>"$(LOG)"; \
	fi
	@tail -100 "$(LOG)"

check-host-contamination:
	@! grep -En '(^|[[:space:]])(cc|gcc|clang|i386-jos-elf-gcc)([[:space:]].*)?(-I|-isystem)[[:space:]]*(/usr/include|/usr/local/include|/opt/homebrew/include|/Library/Developer/CommandLineTools/usr/include|/Applications/Xcode.*/usr/include)' "$(LOG)" >/dev/null || \
		(echo "ERROR: host header path detected in Bash port build log" >&2; \
		echo "Inspect $(LOG) for details." >&2; \
		exit 1)

$(BUILDDIR):
	mkdir -p "$(BUILDDIR)"

# first-pass is listed as a prerequisite so that $(OUT) always re-copies the
# fresh binary after first-pass rebuilds .auxv6-build/bash.
$(OUT): first-pass
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
