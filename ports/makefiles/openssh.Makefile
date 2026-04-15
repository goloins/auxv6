# Makefile.auxv6 - OpenSSH portable first-pass lane for auxv6
#
# This lane is intentionally "non-first-class": it is for early configure/build
# validation while the libc/kernel portability surface is still being finished.
#
# Expected usage:
#   make ports-sync
#   make PORTS=1 ports-progs
# or directly:
#   make -C ports/openssh-10.3p1 -f Makefile.auxv6 all
#
# Output:
#   ssh (ELF artifact used by ports-progs install logic)
#   .auxv6-build/first-pass.log (configure+build transcript)

PORTS_COMMON_CALLER := $(lastword $(MAKEFILE_LIST))
include $(dir $(abspath $(PORTS_COMMON_CALLER)))../../config/ports-common.mk

# PORTS_COMMON_CALLER is captured before the include above, so it correctly
# refers to this Makefile rather than the last file pulled in by libc.mk.
SRCDIR := $(realpath $(dir $(abspath $(PORTS_COMMON_CALLER))))
BUILDDIR := $(SRCDIR)/.auxv6-build
OUT := $(SRCDIR)/ssh
OUT_KEYGEN := $(SRCDIR)/ssh-keygen
OUT_SSHD := $(SRCDIR)/sshd
LOG := $(BUILDDIR)/first-pass.log

TOOL_GCC_INCLUDE := $(shell $(CC) -print-file-name=include)

TARGETFS_DIR ?= $(ROOT)/targetfs
TARGETFS_USR_INCLUDE := $(TARGETFS_DIR)/usr/include
TARGETFS_USR_LIB := $(TARGETFS_DIR)/usr/lib
TARGETFS_ETC_SSH := $(TARGETFS_DIR)/etc/ssh

SSHD_CONF_SRC := $(SRCDIR)/sshd_config
SSH_CONF_SRC := $(SRCDIR)/ssh_config
MODULI_SRC := $(SRCDIR)/moduli

COMMON_CPPFLAGS := -nostdinc -I$(ROOT)/include -I$(ROOT)/include/posix -I$(ROOT)/include/posix/sys -I$(TARGETFS_USR_INCLUDE) -isystem $(TOOL_GCC_INCLUDE)
COMMON_CFLAGS := -fno-pic -static -fno-builtin -fno-strict-aliasing -O2 -Wall -m32 -fno-stack-protector -std=gnu17 -Wno-error=implicit-function-declaration -Wno-error=implicit-int
COMMON_LDFLAGS := -m32 -no-pie -nostdlib -static -Wl,--allow-multiple-definition $(AUXV6_CRT0_OBJ) -L$(TARGETFS_USR_LIB)
LIBC_FALLBACK_A := $(ROOT)/targetfs/lib/libc.a
LIBC_LINK_A := $(firstword $(wildcard $(AUXV6_LIBC_A)) $(wildcard $(LIBC_FALLBACK_A)))
LIBGCC ?= $(shell $(CC) -m32 -print-libgcc-file-name 2>/dev/null)
PORT_LIBS := $(AUXV6_AUXRT_A) $(LIBC_LINK_A) $(LIBGCC)
CONFIGURE_LIBS := -lcrypto $(PORT_LIBS)

# Configure links many probe executables. Link them with auxv6 crt/libc so
# function probes reflect target libc rather than failing as unresolved.
CONFIGURE_LDFLAGS := $(COMMON_LDFLAGS)
CONFIGURE_CPPFLAGS := $(COMMON_CPPFLAGS) -D__linux__=1 -D_GNU_SOURCE=1 -D_BSD_SOURCE=1 -D_DEFAULT_SOURCE=1

# OpenSSH can opportunistically enable many platform/auth integrations.
# Keep first-pass strictly minimal and target-only.
CONFIGURE_ARGS := \
	--host=i386-jos-elf \
	--build=$$(../config.guess 2>/dev/null || echo x86_64-unknown-linux-gnu) \
	--prefix=/usr \
	--sysconfdir=/etc/ssh \
	--without-zlib \
	--without-pam \
	--without-kerberos5 \
	--without-selinux \
	--without-libedit \
	--without-ldns \
	--without-shadow \
	--without-security-key-builtin \
	--disable-security-key \
	--disable-pkcs11 \
	--disable-lastlog \
	--disable-utmp \
	--disable-utmpx \
	--disable-wtmp \
	--disable-wtmpx \
	--disable-libutil \
	--disable-pututline \
	--disable-pututxline \
	--without-openssl-header-check \
	--without-hardening \
	--without-stackprotect \
	--without-retpoline \
	--without-pie

.PHONY: all clean first-pass check-host-contamination install-targetfs

all: first-pass $(OUT) check-host-contamination

first-pass: | $(BUILDDIR)
	@rm -rf "$(BUILDDIR)"
	@mkdir -p "$(BUILDDIR)"
	@echo "openssh: starting first configure/build pass" > "$(LOG)"
	@if [ -z "$(LIBC_LINK_A)" ]; then \
		echo "openssh: missing libc archive; expected $(AUXV6_LIBC_A) or $(LIBC_FALLBACK_A)" | tee -a "$(LOG)" >&2; \
		exit 1; \
	fi
	@if [ ! -f "$(TARGETFS_USR_LIB)/libcrypto.a" ]; then \
		echo "openssh: missing $(TARGETFS_USR_LIB)/libcrypto.a (LibreSSL must be staged first)" | tee -a "$(LOG)" >&2; \
		exit 1; \
	fi
	@if [ ! -f "$(TARGETFS_USR_INCLUDE)/openssl/ssl.h" ]; then \
		echo "openssh: missing $(TARGETFS_USR_INCLUDE)/openssl/ssl.h (LibreSSL headers must be staged first)" | tee -a "$(LOG)" >&2; \
		exit 1; \
	fi
	@cd "$(BUILDDIR)" && \
		env \
			CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" STRIP="$(STRIP)" \
			CPPFLAGS="$(CONFIGURE_CPPFLAGS)" CFLAGS="$(COMMON_CFLAGS)" LDFLAGS="$(CONFIGURE_LDFLAGS)" LIBS="$(CONFIGURE_LIBS)" \
			ac_cv_file__dev_ptmx=no \
			ac_cv_file__dev_ptc=no \
			ac_cv_func_seteuid=yes \
			ac_cv_func_setegid=yes \
			ac_cv_func_setgroups=yes \
			ac_cv_func_setresuid=yes \
			ac_cv_func_setresgid=yes \
			ac_cv_func_socketpair=yes \
			ac_cv_func_sendmsg=yes \
			ac_cv_func_recvmsg=yes \
			ac_cv_func_getspnam=yes \
			ac_cv_func_crypt=yes \
			ac_cv_func_daemon=no \
			ac_cv_func_closefrom=yes \
			ac_cv_func_openpty=yes \
			ac_cv_func_tcgetpgrp=yes \
			ac_cv_func_poll=yes \
			ac_cv_func_ppoll=yes \
			ac_cv_func_select=yes \
			ac_cv_func_getrandom=yes \
			ac_cv_func_getentropy=yes \
			../configure $(CONFIGURE_ARGS) >>"$(LOG)" 2>&1 || \
		echo "openssh: configure failed (non-fatal in staging lane)" >>"$(LOG)"
	@set +e; \
	$(MAKE) -C "$(BUILDDIR)" -j1 \
		MAKEOVERRIDES= \
		LDFLAGS="$(COMMON_LDFLAGS) -L$(BUILDDIR) -L$(BUILDDIR)/openbsd-compat" \
		LIBS="$(CONFIGURE_LIBS)" \
		ssh ssh-keygen sshd >>"$(LOG)" 2>&1; \
	rc=$$?; \
	echo "openssh: first-pass make rc=$$rc" >>"$(LOG)"; \
	if [ $$rc -ne 0 ]; then \
		echo "openssh: first-pass portability failures recorded (non-fatal in staging lane)" >>"$(LOG)"; \
	fi
	@tail -100 "$(LOG)"

$(BUILDDIR):
	mkdir -p "$(BUILDDIR)"

$(OUT): first-pass
	@if [ -f "$(BUILDDIR)/ssh" ] && $(OBJDUMP) -f "$(BUILDDIR)/ssh" 2>/dev/null | grep -q 'file format elf32-i386'; then \
		cp "$(BUILDDIR)/ssh" "$(OUT)"; \
		chmod 0755 "$(OUT)"; \
	else \
		echo "openssh: no usable ELF binary at $(BUILDDIR)/ssh" >&2; \
		exit 1; \
	fi
	@if [ -f "$(BUILDDIR)/ssh-keygen" ] && $(OBJDUMP) -f "$(BUILDDIR)/ssh-keygen" 2>/dev/null | grep -q 'file format elf32-i386'; then \
		cp "$(BUILDDIR)/ssh-keygen" "$(OUT_KEYGEN)"; \
		chmod 0755 "$(OUT_KEYGEN)"; \
	fi
	@if [ -f "$(BUILDDIR)/sshd" ] && $(OBJDUMP) -f "$(BUILDDIR)/sshd" 2>/dev/null | grep -q 'file format elf32-i386'; then \
		cp "$(BUILDDIR)/sshd" "$(OUT_SSHD)"; \
		chmod 0755 "$(OUT_SSHD)"; \
	fi

check-host-contamination:
	@! grep -En '(^|[[:space:]])(cc|gcc|clang|i386-jos-elf-gcc)([[:space:]].*)?(-I|-isystem)[[:space:]]*(/usr/include|/usr/local/include|/opt/homebrew/include|/Library/Developer/CommandLineTools/usr/include|/Applications/Xcode.*/usr/include)' "$(LOG)" >/dev/null || \
		(echo "ERROR: host header path detected in OpenSSH port build log" >&2; \
		echo "Inspect $(LOG) for details." >&2; \
		exit 1)

install-targetfs:
	@install -d "$(TARGETFS_ETC_SSH)"
	@if [ -f "$(SSHD_CONF_SRC)" ] && [ ! -f "$(TARGETFS_ETC_SSH)/sshd_config" ]; then \
		install -m 0644 "$(SSHD_CONF_SRC)" "$(TARGETFS_ETC_SSH)/sshd_config"; \
	fi
	@if [ -f "$(SSH_CONF_SRC)" ] && [ ! -f "$(TARGETFS_ETC_SSH)/ssh_config" ]; then \
		install -m 0644 "$(SSH_CONF_SRC)" "$(TARGETFS_ETC_SSH)/ssh_config"; \
	fi
	@if [ -f "$(MODULI_SRC)" ] && [ ! -f "$(TARGETFS_ETC_SSH)/moduli" ]; then \
		install -m 0644 "$(MODULI_SRC)" "$(TARGETFS_ETC_SSH)/moduli"; \
	fi
	@conf="$(TARGETFS_ETC_SSH)/sshd_config"; \
	if [ -f "$$conf" ]; then \
		if grep -Eq '^[#[:space:]]*Port[[:space:]]+' "$$conf"; then \
			sed -i -E 's|^[#[:space:]]*Port[[:space:]]+.*|Port 22|' "$$conf"; \
		else \
			echo 'Port 22' >> "$$conf"; \
		fi; \
		if grep -Eq '^[#[:space:]]*AddressFamily[[:space:]]+' "$$conf"; then \
			sed -i -E 's|^[#[:space:]]*AddressFamily[[:space:]]+.*|AddressFamily inet|' "$$conf"; \
		else \
			echo 'AddressFamily inet' >> "$$conf"; \
		fi; \
		if grep -Eq '^[#[:space:]]*ListenAddress[[:space:]]+' "$$conf"; then \
			sed -i -E 's|^[#[:space:]]*ListenAddress[[:space:]]+.*|ListenAddress 0.0.0.0|' "$$conf"; \
		else \
			echo 'ListenAddress 0.0.0.0' >> "$$conf"; \
		fi; \
		sed -i -E '/^[#[:space:]]*ListenAddress[[:space:]]+::[[:space:]]*$/d' "$$conf"; \
		if grep -Eq '^[#[:space:]]*HostKey[[:space:]]+/etc/ssh/ssh_host_rsa_key' "$$conf"; then \
			sed -i -E 's|^[#[:space:]]*HostKey[[:space:]]+/etc/ssh/ssh_host_rsa_key.*|HostKey /etc/ssh/ssh_host_rsa_key|' "$$conf"; \
		else \
			echo 'HostKey /etc/ssh/ssh_host_rsa_key' >> "$$conf"; \
		fi; \
		if grep -Eq '^[#[:space:]]*HostKey[[:space:]]+/etc/ssh/ssh_host_ecdsa_key' "$$conf"; then \
			sed -i -E 's|^[#[:space:]]*HostKey[[:space:]]+/etc/ssh/ssh_host_ecdsa_key.*|HostKey /etc/ssh/ssh_host_ecdsa_key|' "$$conf"; \
		else \
			echo 'HostKey /etc/ssh/ssh_host_ecdsa_key' >> "$$conf"; \
		fi; \
		if grep -Eq '^[#[:space:]]*HostKey[[:space:]]+/etc/ssh/ssh_host_ed25519_key' "$$conf"; then \
			sed -i -E 's|^[#[:space:]]*HostKey[[:space:]]+/etc/ssh/ssh_host_ed25519_key.*|HostKey /etc/ssh/ssh_host_ed25519_key|' "$$conf"; \
		else \
			echo 'HostKey /etc/ssh/ssh_host_ed25519_key' >> "$$conf"; \
		fi; \
		if grep -Eq '^[#[:space:]]*PermitRootLogin[[:space:]]+' "$$conf"; then \
			sed -i -E 's|^[#[:space:]]*PermitRootLogin[[:space:]]+.*|PermitRootLogin yes|' "$$conf"; \
		else \
			echo 'PermitRootLogin yes' >> "$$conf"; \
		fi; \
		if grep -Eq '^[#[:space:]]*KbdInteractiveAuthentication[[:space:]]+' "$$conf"; then \
			sed -i -E 's|^[#[:space:]]*KbdInteractiveAuthentication[[:space:]]+.*|KbdInteractiveAuthentication no|' "$$conf"; \
		else \
			echo 'KbdInteractiveAuthentication no' >> "$$conf"; \
		fi; \
		sed -i -E '/^[#[:space:]]*UsePAM[[:space:]]+/d' "$$conf"; \
		if grep -Eq '^[#[:space:]]*UsePrivilegeSeparation[[:space:]]+' "$$conf"; then \
			sed -i -E 's|^[#[:space:]]*UsePrivilegeSeparation[[:space:]]+.*|UsePrivilegeSeparation no|' "$$conf"; \
		else \
			echo 'UsePrivilegeSeparation no' >> "$$conf"; \
		fi; \
		if grep -Eq '^[#[:space:]]*PidFile[[:space:]]+' "$$conf"; then \
			sed -i -E 's|^[#[:space:]]*PidFile[[:space:]]+.*|PidFile /var/run/sshd.pid|' "$$conf"; \
		else \
			echo 'PidFile /var/run/sshd.pid' >> "$$conf"; \
		fi; \
		if grep -Eq '^[#[:space:]]*PasswordAuthentication[[:space:]]+' "$$conf"; then \
			sed -i -E 's|^[#[:space:]]*PasswordAuthentication[[:space:]]+.*|PasswordAuthentication yes|' "$$conf"; \
		else \
			echo 'PasswordAuthentication yes' >> "$$conf"; \
		fi; \
	fi
	@for k in "$(TARGETFS_ETC_SSH)/ssh_host_rsa_key" "$(TARGETFS_ETC_SSH)/ssh_host_ecdsa_key" "$(TARGETFS_ETC_SSH)/ssh_host_ed25519_key"; do \
		if [ -f "$$k" ]; then chmod 0600 "$$k"; fi; \
		if [ -f "$$k.pub" ]; then chmod 0644 "$$k.pub"; fi; \
	done

clean:
	rm -rf "$(BUILDDIR)" "$(OUT)" "$(OUT_KEYGEN)" "$(OUT_SSHD)"
