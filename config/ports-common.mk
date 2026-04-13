ifndef PORTS_COMMON_CALLER
PORTS_COMMON_CALLER := $(lastword $(MAKEFILE_LIST))
endif

PORTS_COMMON_ROOT_REL ?= ../..

ifndef ROOT
ROOT := $(realpath $(dir $(abspath $(PORTS_COMMON_CALLER)))$(PORTS_COMMON_ROOT_REL))
endif

include $(ROOT)/config/libc.mk

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

# Use plain assignment so make's built-in default CC=cc (origin "default") is
# properly overridden with the cross-compiler.  ?= only fires for origin
# "undefined" and silently loses to the built-in default.  Command-line
# overrides (e.g. make CC=clang) still win over plain = assignments.
ifneq ($(filter default undefined,$(origin CC)),)
CC = $(TOOLPREFIX)gcc
endif
ifneq ($(filter default undefined,$(origin LD)),)
LD = $(TOOLPREFIX)ld
endif
ifneq ($(filter default undefined,$(origin AR)),)
AR = $(TOOLPREFIX)ar
endif
ifneq ($(filter default undefined,$(origin RANLIB)),)
RANLIB = $(TOOLPREFIX)ranlib
endif
ifneq ($(filter default undefined,$(origin STRIP)),)
STRIP = $(TOOLPREFIX)strip
endif
ifneq ($(filter default undefined,$(origin OBJDUMP)),)
OBJDUMP = $(TOOLPREFIX)objdump
endif

LIBGCC ?= $(shell $(CC) -m32 -print-libgcc-file-name 2>/dev/null)