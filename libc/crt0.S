#include "syscall.h"
#include "traps.h"

.globl _start
.extern environ
_start:
  movl 4(%esp), %eax
  movl 8(%esp), %edx
  movl 12(%esp), %ecx
  movl %ecx, environ
  pushl %ecx
  pushl %edx
  pushl %eax
  call main
  pushl %eax
  call exit

1:
  jmp 1b