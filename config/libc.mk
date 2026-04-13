ifndef ROOT
ROOT := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST))))..)
endif

AUXV6_CRT0_OBJ := $(ROOT)/libc/crt0.o
AUXV6_LIBC_A := $(ROOT)/libc/libc.a
AUXV6_AUXRT_A := $(ROOT)/libc/libauxrt.a
AUXV6_X11_A := $(ROOT)/user/libX11.a