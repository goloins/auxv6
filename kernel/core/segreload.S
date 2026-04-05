#include "mmu.h"

# Reload segment selectors after lgdt updates the GDT.
# Needed for GRUB boot paths where incoming selectors may not match
# kernel descriptor indices in the new GDT.

.globl segreload
segreload:
  movw $(SEG_KDATA<<3), %ax
  movw %ax, %ds
  movw %ax, %es
  movw %ax, %ss
  movw %ax, %fs
  movw %ax, %gs
  ljmp $(SEG_KCODE<<3), $1f
1:
  ret
