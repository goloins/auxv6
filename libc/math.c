/*
 * libc/math.c - core math functions needed by POSIX ports
 */

#include "types.h"
#include "errno.h"
#include "math.h"

double
pow(double x, double y)
{
  long exp;
  double result;
  int neg;

  /* auxv6 currently supports integer exponents in libc pow(). */
  exp = (long)y;
  if ((double)exp != y) {
    errno = EDOM;
    return 0.0;
  }

  if (exp == 0)
    return 1.0;

  neg = 0;
  if (exp < 0) {
    neg = 1;
    exp = -exp;
  }

  result = 1.0;
  while (exp > 0) {
    if (exp & 1L)
      result *= x;
    exp >>= 1;
    if (exp)
      x *= x;
  }

  if (neg) {
    if (result == 0.0) {
      errno = ERANGE;
      return HUGE_VAL;
    }
    return 1.0 / result;
  }

  return result;
}
