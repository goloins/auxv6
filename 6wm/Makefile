# 6wm/Makefile — build rules for the 6wm window manager
#
# 6wm is a first-party auxv6 program; it lives in 6wm/ at the project
# root and is invoked from the top-level Makefile via:
#   $(MAKE) -f 6wm/Makefile all

6WM_DIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ROOT    := $(realpath $(6WM_DIR)/..)

include $(ROOT)/config/ports-common.mk

OUT := $(6WM_DIR)/_6wm

X11INC ?= $(ROOT)/include

INCS := \
	-I$(6WM_DIR) \
	-I$(X11INC) \
	-I$(ROOT)/include \
	-I$(ROOT)/include/posix \
	-I$(ROOT)/include/posix/sys

CPPFLAGS := -DAUXV6

CFLAGS := \
	-fno-pic -static -fno-builtin -fno-strict-aliasing -O2 \
	-Wall -MD -ggdb -m32 \
	-fno-omit-frame-pointer -nostdinc -fno-stack-protector \
	-std=c99 \
	$(INCS) $(CPPFLAGS)

LDFLAGS := \
	-N -e _start -Ttext 0 \
	-m $(shell $(LD) -V 2>/dev/null | grep elf_i386 | head -1)

SRC := \
	$(6WM_DIR)/6wm.c   \
	$(6WM_DIR)/atoms.c  \
	$(6WM_DIR)/client.c \
	$(6WM_DIR)/draw.c   \
	$(6WM_DIR)/focus.c  \
	$(6WM_DIR)/frame.c  \
	$(6WM_DIR)/menu.c   \
	$(6WM_DIR)/stack.c

OBJ := $(SRC:.c=.o)

RUNTIME_INPUTS := \
	$(AUXV6_CRT0_OBJ) \
	$(AUXV6_X11_A)    \
	$(AUXV6_AUXRT_A)  \
	$(AUXV6_LIBC_A)

.PHONY: all clean toolchain-check

all: toolchain-check $(OUT)

toolchain-check:
	@$(CC) -m32 -dM -E -x c /dev/null | grep -q '__i386__' || \
		(echo "ERROR: $(CC) is not producing i386 output with -m32" >&2; exit 1)

$(6WM_DIR)/%.o: $(6WM_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(RUNTIME_INPUTS):
	$(MAKE) -C $(ROOT) $(patsubst $(ROOT)/%,%,$@)

$(OUT): $(OBJ) $(RUNTIME_INPUTS)
	$(LD) $(LDFLAGS) -o $@ \
		$(AUXV6_CRT0_OBJ) $(OBJ) \
		$(AUXV6_X11_A) $(AUXV6_AUXRT_A) $(AUXV6_LIBC_A) \
		$(LIBGCC)
	$(OBJDUMP) -S $@ > $(6WM_DIR)/6wm.asm || true

clean:
	rm -f $(OBJ) $(OUT) $(SRC:.c=.d) $(6WM_DIR)/6wm.asm

-include $(OBJ:.o=.d)
