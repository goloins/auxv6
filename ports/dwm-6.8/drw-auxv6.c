/*
 * drw-auxv6.c - minimal drawing backend for auxv6/x6
 * Keeps dwm logic intact while x6 renderer matures.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include "graphics/user_font.h"

#include "drw.h"
#include "util.h"

static unsigned long
parse_rgb24(const char *s)
{
	int i;
	unsigned long v;

	if (!s || s[0] != '#')
		return 0;
	v = 0;
	for (i = 1; i <= 6; i++) {
		char c = s[i];
		if (c >= '0' && c <= '9')
			v = (v << 4) | (unsigned long)(c - '0');
		else if (c >= 'a' && c <= 'f')
			v = (v << 4) | (unsigned long)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			v = (v << 4) | (unsigned long)(c - 'A' + 10);
		else
			return 0;
	}
	return v & 0x00ffffffUL;
}

Drw *
drw_create(Display *dpy, int screen, Window root, unsigned int w, unsigned int h)
{
	Drw *drw = ecalloc(1, sizeof(Drw));
	drw->dpy = dpy;
	drw->screen = screen;
	drw->root = root;
	drw->w = w;
	drw->h = h;
	drw->drawable = root;
	drw->gc = XCreateGC(dpy, root, 0, NULL);
	return drw;
}

void
drw_resize(Drw *drw, unsigned int w, unsigned int h)
{
	if (!drw)
		return;
	drw->w = w;
	drw->h = h;
}

void
drw_free(Drw *drw)
{
	if (!drw)
		return;
	drw_fontset_free(drw->fonts);
	free(drw);
}

Fnt *
drw_fontset_create(Drw *drw, const char *fonts[], size_t fontcount)
{
	Fnt *font = ecalloc(1, sizeof(Fnt));
	font->dpy = drw ? drw->dpy : NULL;
	font->ufont = user_font_builtin_montecarlo();
	font->h = font->ufont ? (unsigned int)font->ufont->size : 16U;
	font->next = NULL;
	if (drw)
		drw->fonts = font;
	(void)fonts;
	(void)fontcount;
	return font;
}

void
drw_fontset_free(Fnt *set)
{
	while (set) {
		Fnt *next = set->next;
		free(set);
		set = next;
	}
}

unsigned int
drw_fontset_getwidth(Drw *drw, const char *text)
{
	const struct user_font *uf;
	int n;

	(void)drw;
	if (!text)
		return 0;
	uf = (drw && drw->fonts) ? drw->fonts->ufont : user_font_builtin_montecarlo();
	n = (int)strlen(text);
	return (unsigned int)user_font_text_width(uf, text, n);
}

unsigned int
drw_fontset_getwidth_clamp(Drw *drw, const char *text, unsigned int n)
{
	unsigned int w = drw_fontset_getwidth(drw, text);
	if (w > n)
		return n;
	return w;
}

void
drw_font_getexts(Fnt *font, const char *text, unsigned int len, unsigned int *w, unsigned int *h)
{
	const struct user_font *uf;

	uf = font ? font->ufont : user_font_builtin_montecarlo();
	if (w)
		*w = (unsigned int)user_font_text_width(uf, text, (int)len);
	if (h)
		*h = (unsigned int)(uf ? uf->size : 16);
}

void
drw_clr_create(Drw *drw, Clr *dest, const char *clrname)
{
	(void)drw;
	if (dest)
		dest->pixel = parse_rgb24(clrname);
}

void
drw_clr_free(Drw *drw, Clr *c)
{
	(void)drw;
	(void)c;
}

Clr *
drw_scm_create(Drw *drw, const char *clrnames[], size_t clrcount)
{
	Clr *scm = ecalloc(clrcount, sizeof(Clr));
	size_t i;
	for (i = 0; i < clrcount; i++)
		drw_clr_create(drw, &scm[i], clrnames ? clrnames[i] : NULL);
	return scm;
}

void
drw_scm_free(Drw *drw, Clr *scm, size_t clrcount)
{
	(void)drw;
	(void)clrcount;
	free(scm);
}

Cur *
drw_cur_create(Drw *drw, int shape)
{
	Cur *cur = ecalloc(1, sizeof(Cur));
	cur->cursor = XCreateFontCursor(drw->dpy, shape);
	return cur;
}

void
drw_cur_free(Drw *drw, Cur *cursor)
{
	if (!cursor)
		return;
	XFreeCursor(drw->dpy, cursor->cursor);
	free(cursor);
}

void
drw_setfontset(Drw *drw, Fnt *set)
{
	if (drw)
		drw->fonts = set;
}

void
drw_setscheme(Drw *drw, Clr *scm)
{
	if (drw)
		drw->scheme = scm;
}

void
drw_rect(Drw *drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert)
{
	if (!drw || !drw->scheme)
		return;
	XSetForeground(drw->dpy, drw->gc,
	               invert ? drw->scheme[ColBg].pixel : drw->scheme[ColFg].pixel);
	if (filled)
		XFillRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w, h);
	else
		XDrawRectangle(drw->dpy, drw->drawable, drw->gc, x, y,
		               w > 0 ? w - 1 : 0, h > 0 ? h - 1 : 0);
}

int
drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, const char *text, int invert)
{
	unsigned int tw;
	int tx;
	int ty;
	const struct user_font *uf;

	if (!drw || !drw->scheme)
		return x + (int)w;

	XSetForeground(drw->dpy, drw->gc,
	               invert ? drw->scheme[ColFg].pixel : drw->scheme[ColBg].pixel);
	XFillRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w, h);

	tx = x + (int)lpad;
	tw = drw_fontset_getwidth(drw, text ? text : "");
	if (tw > w)
		tw = w;

	if (tw > 0) {
		uf = (drw->fonts && drw->fonts->ufont) ? drw->fonts->ufont : user_font_builtin_montecarlo();
		ty = y + (uf ? uf->ascent : 12);
		XSetForeground(drw->dpy, drw->gc,
		               invert ? drw->scheme[ColBg].pixel : drw->scheme[ColFg].pixel);
		XDrawString(drw->dpy, drw->drawable, drw->gc, tx, ty, text ? text : "", (int)strlen(text ? text : ""));
	}

	return x + (int)w;
}

void
drw_map(Drw *drw, Window win, int x, int y, unsigned int w, unsigned int h)
{
	(void)drw;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	XMapWindow(drw->dpy, win);
}
