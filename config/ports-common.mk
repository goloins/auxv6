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

CC ?= $(TOOLPREFIX)gcc
LD ?= $(TOOLPREFIX)ld
AR ?= $(TOOLPREFIX)ar
RANLIB ?= $(TOOLPREFIX)ranlib
STRIP ?= $(TOOLPREFIX)strip
OBJDUMP ?= $(TOOLPREFIX)objdump

LIBGCC ?= $(shell $(CC) -m32 -print-libgcc-file-name 2>/dev/null)