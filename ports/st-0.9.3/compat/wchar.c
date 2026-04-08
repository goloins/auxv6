/* AUXV6_ST_HACK: minimal wchar helpers for st integration. */
#include "wchar.h"

wchar_t *
wcschr(const wchar_t *s, wchar_t c)
{
	if (!s)
		return (wchar_t *)0;
	while (*s) {
		if (*s == c)
			return (wchar_t *)s;
		s++;
	}
	return c == 0 ? (wchar_t *)s : (wchar_t *)0;
}

int
wcwidth(wchar_t wc)
{
	if (wc == 0)
		return 0;
	if (wc < 32 || (wc >= 0x7f && wc < 0xa0))
		return -1;
	if (wc < 0x1100)
		return 1;
	return 1;
}
