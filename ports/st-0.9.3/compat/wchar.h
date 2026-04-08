#ifndef AUXV6_ST_COMPAT_WCHAR_H
#define AUXV6_ST_COMPAT_WCHAR_H

#include <stddef.h>

#ifndef __WCHAR_TYPE__
typedef int wchar_t;
#endif

wchar_t *wcschr(const wchar_t *s, wchar_t c);
int wcwidth(wchar_t wc);

#endif
