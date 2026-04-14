# Makefile.auxv6 - LibreSSL portable first-pass lane for auxv6
#
# This lane is intentionally "non-first-class": it is for early configure/build
# validation while the libc/kernel portability surface is still being finished.
#
# Expected usage:
#   make ports-sync
#   make PORTS=1 ports-progs
# or directly:
#   make -C ports/libressl-4.2.1 -f Makefile.auxv6 all
#
# Output:
#   _libressl (small launcher stub used by ports-progs install logic)
#   .auxv6-build/first-pass.log (configure+build transcript)

PORTS_COMMON_CALLER := $(lastword $(MAKEFILE_LIST))
ROOT := $(realpath $(dir $(abspath $(PORTS_COMMON_CALLER)))../..)
include $(ROOT)/config/libc.mk
SRCDIR := $(realpath $(dir $(abspath $(PORTS_COMMON_CALLER))))
BUILDDIR := $(SRCDIR)/.auxv6-build
OUT := $(SRCDIR)/_libressl
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
OBJDUMP := $(TOOLPREFIX)objdump

TOOL_GCC_INCLUDE := $(shell $(CC) -print-file-name=include)

COMMON_CPPFLAGS := -nostdinc -I$(ROOT)/include -I$(ROOT)/include/posix -I$(ROOT)/include/posix/sys
COMMON_CFLAGS := -fno-pic -static -fno-builtin -fno-strict-aliasing -O2 -Wall -m32 -fno-stack-protector -std=gnu17 -Wno-error=implicit-function-declaration -Wno-error=implicit-int
COMMON_LDFLAGS := -static
LIBGCC ?= $(shell $(CC) -m32 -print-libgcc-file-name 2>/dev/null)
LIBC_FALLBACK_A := $(ROOT)/targetfs/lib/libc.a
LIBC_LINK_A := $(firstword $(wildcard $(AUXV6_LIBC_A)) $(wildcard $(LIBC_FALLBACK_A)))
PORT_LDFLAGS := -m32 -no-pie -nostdlib -static -Wl,--allow-multiple-definition $(AUXV6_CRT0_OBJ)
PORT_LIBS := $(AUXV6_AUXRT_A) $(LIBC_LINK_A) $(LIBGCC)
TARGETFS_DIR ?= $(ROOT)/targetfs
TARGETFS_LIB := $(TARGETFS_DIR)/lib
TARGETFS_USR_BIN := $(TARGETFS_DIR)/usr/bin
TARGETFS_USR_INCLUDE := $(TARGETFS_DIR)/usr/include
TARGETFS_USR_INCLUDE_OPENSSL := $(TARGETFS_USR_INCLUDE)/openssl
TARGETFS_USR_LIB_PKGCONFIG := $(TARGETFS_DIR)/usr/lib/pkgconfig
TARGETFS_ETC_SSL := $(TARGETFS_DIR)/etc/ssl
CONFIGURE_CPPFLAGS := $(COMMON_CPPFLAGS) -isystem $(TOOL_GCC_INCLUDE) \
	-DHAVE_ARC4RANDOM=1 -DHAVE_ARC4RANDOM_BUF=1 -DHAVE_ARC4RANDOM_UNIFORM=1 \
	-D__linux__=1
# Configure links probe executables; avoid crt0/libc requirements for cross checks.
CONFIGURE_LDFLAGS := -nostdlib -nostartfiles

.PHONY: all clean first-pass check-host-contamination stage-targetfs

all: stage-targetfs

first-pass: | $(BUILDDIR)
	@echo "libressl: starting first configure/build pass" > "$(LOG)"
	@cd "$(BUILDDIR)" && \
		env \
			CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" STRIP="$(STRIP)" \
			ac_cv_func_strcasecmp=yes ac_cv_func_strncasecmp=yes \
			CPPFLAGS="$(CONFIGURE_CPPFLAGS)" CFLAGS="$(COMMON_CFLAGS)" LDFLAGS="$(CONFIGURE_LDFLAGS)" \
			../configure \
				--host=i386-jos-elf \
				--build=$$(../config.guess 2>/dev/null || echo x86_64-unknown-linux-gnu) \
				--disable-shared \
				--enable-static \
				--disable-nc \
				--disable-tests \
				--without-openssldir \
				--prefix=/usr \
				>>"$(LOG)" 2>&1 || \
		echo "libressl: configure failed (non-fatal in staging lane)" >>"$(LOG)"
	@set +e; \
	if [ -z "$(LIBC_LINK_A)" ]; then \
		echo "libressl: missing libc archive; expected $(AUXV6_LIBC_A) or $(LIBC_FALLBACK_A)" >>"$(LOG)"; \
		echo "libressl: missing libc archive; expected $(AUXV6_LIBC_A) or $(LIBC_FALLBACK_A)" >&2; \
		exit 1; \
	fi; \
	$(MAKE) -C "$(BUILDDIR)/crypto" -k -j1 >>"$(LOG)" 2>&1; \
	$(MAKE) -C "$(BUILDDIR)/ssl" -k -j1 >>"$(LOG)" 2>&1; \
	$(MAKE) -C "$(BUILDDIR)/tls" -k -j1 >>"$(LOG)" 2>&1; \
	$(MAKE) -C "$(BUILDDIR)/apps/openssl" -k -j1 LDFLAGS="$(PORT_LDFLAGS)" LIBS="$(PORT_LIBS)" >>"$(LOG)" 2>&1; \
	rc=$$?; \
	echo "libressl: first-pass make rc=$$rc" >>"$(LOG)"; \
	if [ $$rc -ne 0 ]; then \
		echo "libressl: first-pass portability failures recorded (non-fatal in staging lane)" >>"$(LOG)"; \
	fi
	@tail -100 "$(LOG)"

$(BUILDDIR):
	mkdir -p "$(BUILDDIR)"

$(OUT): first-pass
	@for cand in "$(BUILDDIR)/apps/openssl" "$(BUILDDIR)/apps/openssl/openssl"; do \
		if [ -f "$$cand" ] && $(OBJDUMP) -f "$$cand" 2>/dev/null | grep -q 'file format elf32-i386'; then \
			cp "$$cand" "$(OUT)"; \
			chmod 0755 "$(OUT)"; \
			exit 0; \
		fi; \
	done; \
	echo "libressl: no usable ELF binary at $(BUILDDIR)/apps/openssl or $(BUILDDIR)/apps/openssl/openssl" >&2; \
	exit 1

clean:
	rm -rf "$(BUILDDIR)" "$(OUT)"

check-host-contamination: first-pass
	@! grep -En '(^|[[:space:]])(cc|gcc|clang|i386-jos-elf-gcc)([[:space:]].*)?(-I|-isystem)[[:space:]]*(/usr/include|/usr/local/include|/opt/homebrew/include|/Library/Developer/CommandLineTools/usr/include|/Applications/Xcode.*/usr/include)' "$(LOG)" >/dev/null || \
		(echo "ERROR: host header path detected in LibreSSL port build log" >&2; \
		echo "Inspect $(LOG) for details." >&2; \
		exit 1)

stage-targetfs: $(OUT) check-host-contamination
	@install -d "$(TARGETFS_LIB)" "$(TARGETFS_USR_BIN)" "$(TARGETFS_USR_INCLUDE)" "$(TARGETFS_USR_INCLUDE_OPENSSL)" "$(TARGETFS_USR_LIB_PKGCONFIG)" "$(TARGETFS_ETC_SSL)" "$(TARGETFS_ETC_SSL)/certs"
	@if [ -f "$(BUILDDIR)/apps/openssl/openssl" ]; then \
		install -m 0755 "$(BUILDDIR)/apps/openssl/openssl" "$(TARGETFS_USR_BIN)/openssl"; \
	fi
	@if [ -f "$(BUILDDIR)/crypto/.libs/libcrypto.a" ]; then \
		install -m 0644 "$(BUILDDIR)/crypto/.libs/libcrypto.a" "$(TARGETFS_LIB)/libcrypto.a"; \
	fi
	@if [ -f "$(BUILDDIR)/ssl/.libs/libssl.a" ]; then \
		install -m 0644 "$(BUILDDIR)/ssl/.libs/libssl.a" "$(TARGETFS_LIB)/libssl.a"; \
	fi
	@if [ -f "$(BUILDDIR)/tls/.libs/libtls.a" ]; then \
		install -m 0644 "$(BUILDDIR)/tls/.libs/libtls.a" "$(TARGETFS_LIB)/libtls.a"; \
	fi
	@install -m 0644 "$(SRCDIR)/include/tls.h" "$(TARGETFS_USR_INCLUDE)/tls.h"
	@find "$(SRCDIR)/include/openssl" -maxdepth 1 -type f -name '*.h' -exec install -m 0644 {} "$(TARGETFS_USR_INCLUDE_OPENSSL)/" \;
	@for pc in libtls.pc libcrypto.pc libssl.pc openssl.pc; do \
		if [ -f "$(BUILDDIR)/$$pc" ]; then \
			install -m 0644 "$(BUILDDIR)/$$pc" "$(TARGETFS_USR_LIB_PKGCONFIG)/$$pc"; \
		fi; \
	done
	@for cfg in cert.pem openssl.cnf x509v3.cnf; do \
		install -m 0644 "$(SRCDIR)/$$cfg" "$(TARGETFS_ETC_SSL)/$$cfg"; \
	done
