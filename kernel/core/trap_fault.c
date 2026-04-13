#include "types.h"
#include "defs.h"
#include "traps.h"
#include "signal.h"

// Map x86 trap number to Unix signal number.
// Returns 0 if no mapping (unknown trap).
int
trap_to_signal(int trapno)
{
  switch(trapno) {
  case T_DIVIDE:   return SIGFPE;    // Divide by zero
  case T_DEBUG:    return SIGTRAP;   // Debug exception
  case T_BRKPT:    return SIGTRAP;   // Breakpoint (int3)
  case T_OFLOW:    return SIGFPE;    // Overflow
  case T_BOUND:    return SIGSEGV;   // Bounds check failed
  case T_ILLOP:    return SIGILL;    // Illegal opcode
  case T_DEVICE:   return SIGFPE;    // Device not available
  case T_GPFLT:    return SIGSEGV;   // General protection fault
  case T_PGFLT:    return SIGSEGV;   // Page fault
  case T_FPERR:    return SIGFPE;    // x87 FPU error
  case T_ALIGN:    return SIGBUS;    // Alignment check
  case T_SIMDERR:  return SIGFPE;    // SIMD floating point
  default:         return 0;         // Unknown
  }
}