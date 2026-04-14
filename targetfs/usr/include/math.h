#ifndef _AUXV6_MATH_H
#define _AUXV6_MATH_H

/* Minimal but POSIX-usable math.h surface for auxv6 */

#include "errno.h"

#ifndef HUGE
#define HUGE 1.7976931348623157e+308
#endif

#ifndef HUGE_VAL
#define HUGE_VAL HUGE
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double pow(double x, double y);

/* ceil-like function for floats */
static inline float ceilf(float x) {
  int i = (int)x;
  return (x > (float)i) ? (float)(i + 1) : (float)i;
}

static inline double ceil(double x) {
  int i = (int)x;
  return (x > (double)i) ? (double)(i + 1) : (double)i;
}

static inline float floorf(float x) {
  return (float)(int)x;
}

static inline double floor(double x) {
  return (double)(int)x;
}

#endif  /* _AUXV6_MATH_H */
