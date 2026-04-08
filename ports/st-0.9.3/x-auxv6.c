/* AUXV6_ST_HACK: Minimal x backend for auxv6 x11 shim.
 *
 * This intentionally bypasses upstream Xft/XIM/fontconfig-heavy x.c so we can
 * integrate and test st core terminal behavior early. Rewire to upstream x.c
 * as x11/xft/xim compatibility lands.
 */
#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

char *argv0;
#include "arg.h"
#include "st.h"
#include "win.h"
#include "config.h"

typedef struct {
	Display *dpy;
	Window win;
	GC gc;
	int scr;
	int w;
	int h;
	int tw;
	int th;
	int cw;
	int ch;
	int mode;
	Atom wmdelete;
	long event_mask;
} AuxWin;

static AuxWin auxw;
static char *opt_io;
static char *opt_line;
static char **opt_cmd;
static char *opt_title;

static char *aux_clipboard;
static uint32_t aux_palette[260];

static uint32_t
aux_rgb(uint r, uint g, uint b)
{
	return ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff);
}

static int
aux_hex(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return 10 + (c - 'a');
	if (c >= 'A' && c <= 'F')
		return 10 + (c - 'A');
	return -1;
}

static int
aux_parse_color(const char *s, uint32_t *out)
{
	int a, b, c, d, e, f;

	if (!s || !out)
		return -1;
	if (s[0] == '#' && strlen(s) == 7) {
		a = aux_hex(s[1]);
		b = aux_hex(s[2]);
		c = aux_hex(s[3]);
		d = aux_hex(s[4]);
		e = aux_hex(s[5]);
		f = aux_hex(s[6]);
		if (a < 0 || b < 0 || c < 0 || d < 0 || e < 0 || f < 0)
			return -1;
		*out = aux_rgb((a << 4) | b, (c << 4) | d, (e << 4) | f);
		return 0;
	}

	if (!strcmp(s, "black")) { *out = aux_rgb(0, 0, 0); return 0; }
	if (!strcmp(s, "white")) { *out = aux_rgb(255, 255, 255); return 0; }
	if (!strcmp(s, "gray50")) { *out = aux_rgb(128, 128, 128); return 0; }
	if (!strcmp(s, "gray90")) { *out = aux_rgb(230, 230, 230); return 0; }
	if (!strcmp(s, "red")) { *out = aux_rgb(255, 0, 0); return 0; }
	if (!strcmp(s, "red3")) { *out = aux_rgb(205, 0, 0); return 0; }
	if (!strcmp(s, "green")) { *out = aux_rgb(0, 255, 0); return 0; }
	if (!strcmp(s, "green3")) { *out = aux_rgb(0, 205, 0); return 0; }
	if (!strcmp(s, "yellow")) { *out = aux_rgb(255, 255, 0); return 0; }
	if (!strcmp(s, "yellow3")) { *out = aux_rgb(205, 205, 0); return 0; }
	if (!strcmp(s, "blue")) { *out = aux_rgb(0, 0, 255); return 0; }
	if (!strcmp(s, "blue2")) { *out = aux_rgb(0, 0, 238); return 0; }
	if (!strcmp(s, "magenta")) { *out = aux_rgb(255, 0, 255); return 0; }
	if (!strcmp(s, "magenta3")) { *out = aux_rgb(205, 0, 205); return 0; }
	if (!strcmp(s, "cyan")) { *out = aux_rgb(0, 255, 255); return 0; }
	if (!strcmp(s, "cyan3")) { *out = aux_rgb(0, 205, 205); return 0; }
	return -1;
}

static uint32_t
aux_color_of(uint32_t c, int isfg)
{
	uint idx;
	uint cidx;

	if (IS_TRUECOL(c))
		return c & 0x00ffffff;

	/* st uses 256..259 for special/default colors; do not truncate to 8 bits. */
	cidx = c;
	if (cidx < LEN(aux_palette) && aux_palette[cidx] != 0)
		return aux_palette[cidx];

	idx = c & 0xff;
	if (idx < LEN(aux_palette) && aux_palette[idx] != 0)
		return aux_palette[idx];
	return isfg ? aux_rgb(230, 230, 230) : aux_rgb(0, 0, 0);
}

void
xloadcols(void)
{
	size_t i;
	uint32_t rgb;

	for (i = 0; i < LEN(aux_palette); i++)
		aux_palette[i] = 0;
	for (i = 0; i < LEN(colorname); i++) {
		if (!colorname[i])
			continue;
		if (aux_parse_color(colorname[i], &rgb) == 0)
			aux_palette[i] = rgb;
	}
}

int
xsetcolorname(int x, const char *name)
{
	uint32_t rgb;

	if (x < 0 || x >= (int)LEN(aux_palette))
		return 1;
	if (!name)
		return 1;
	if (aux_parse_color(name, &rgb) < 0)
		return 1;
	aux_palette[x] = rgb;
	return 0;
}

int
xgetcolor(int x, unsigned char *r, unsigned char *g, unsigned char *b)
{
	uint32_t rgb;

	if (!r || !g || !b)
		return 1;
	if (x < 0 || x >= (int)LEN(aux_palette))
		return 1;
	rgb = aux_palette[x];
	*r = (rgb >> 16) & 0xff;
	*g = (rgb >> 8) & 0xff;
	*b = rgb & 0xff;
	return 0;
}

static void
aux_setfg(uint32_t rgb)
{
	XSetForeground(auxw.dpy, auxw.gc, rgb & 0x00ffffff);
}

static void
aux_fill(int x, int y, int w, int h, uint32_t rgb)
{
	if (w <= 0 || h <= 0)
		return;
	aux_setfg(rgb);
	XFillRectangle(auxw.dpy, auxw.win, auxw.gc, x, y, (unsigned int)w, (unsigned int)h);
}

static void
aux_draw_glyph_cell(Glyph g, int cx, int cy)
{
	char ch[2];
	uint32_t fg;
	uint32_t bg;
	int px;
	int py;

	fg = aux_color_of(g.fg, 1);
	bg = aux_color_of(g.bg, 0);
	if (g.mode & ATTR_REVERSE) {
		uint32_t t = fg;
		fg = bg;
		bg = t;
	}

	px = borderpx + cx * auxw.cw;
	py = borderpx + cy * auxw.ch;
	aux_fill(px, py, auxw.cw, auxw.ch, bg);

	if (g.u == ' ' || g.u == 0)
		return;
	ch[0] = (g.u >= 32 && g.u < 127) ? (char)g.u : '?';
	ch[1] = 0;
	aux_setfg(fg);
	XDrawString(auxw.dpy, auxw.win, auxw.gc, px, py + auxw.ch - 3, ch, 1);
}

void
xdrawline(Line line, int x1, int y1, int x2)
{
	int x;

	for (x = x1; x < x2; x++)
		aux_draw_glyph_cell(line[x], x, y1);
}

void
xdrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og)
{
	(void)ox;
	(void)oy;
	(void)og;
	g.mode ^= ATTR_REVERSE;
	aux_draw_glyph_cell(g, cx, cy);
}

int
xstartdraw(void)
{
	return 1;
}

void
xfinishdraw(void)
{
	XFlush(auxw.dpy);
}

void
xseticontitle(char *p)
{
	(void)p;
}

void
xsettitle(char *p)
{
	if (!p)
		p = "st";
	XStoreName(auxw.dpy, auxw.win, p);
}

int
xsetcursor(int cursor)
{
	if (cursor < 0 || cursor > 7)
		return 1;
	return 0;
}

void
xsetmode(int set, unsigned int flags)
{
	if (set)
		auxw.mode |= (int)flags;
	else
		auxw.mode &= ~(int)flags;
}

void
xsetpointermotion(int set)
{
	if (set)
		auxw.event_mask |= PointerMotionMask;
	else
		auxw.event_mask &= ~PointerMotionMask;
	XSelectInput(auxw.dpy, auxw.win, auxw.event_mask);
}

void
xsetsel(char *p)
{
	free(aux_clipboard);
	aux_clipboard = p ? xstrdup(p) : NULL;
}

void
xximspot(int x, int y)
{
	(void)x;
	(void)y;
}

void
xbell(void)
{
}

void
xclipcopy(void)
{
	xsetsel(getsel());
}

static void
clipcopy(const Arg *dummy)
{
	(void)dummy;
	xclipcopy();
}

static void
clippaste(const Arg *dummy)
{
	(void)dummy;
	if (aux_clipboard)
		ttywrite(aux_clipboard, strlen(aux_clipboard), 1);
}

static void
selpaste(const Arg *dummy)
{
	clippaste(dummy);
}

static void
numlock(const Arg *dummy)
{
	(void)dummy;
}

static void
zoom(const Arg *arg)
{
	(void)arg;
}

static void
zoomabs(const Arg *arg)
{
	(void)arg;
}

static void
zoomreset(const Arg *arg)
{
	(void)arg;
}

static void
ttysend(const Arg *arg)
{
	if (arg && arg->s)
		ttywrite(arg->s, strlen(arg->s), 1);
}

static void
aux_cresize(int w, int h)
{
	int col;
	int row;

	auxw.w = w;
	auxw.h = h;
	auxw.tw = MAX(w - 2 * borderpx, 1);
	auxw.th = MAX(h - 2 * borderpx, 1);
	col = MAX(auxw.tw / auxw.cw, 1);
	row = MAX(auxw.th / auxw.ch, 1);
	tresize(col, row);
	ttyresize(col, row);
}

static void
aux_kpress(XEvent *ev)
{
	char buf[8];
	KeySym ksym;
	int n;

	n = XLookupString(&ev->xkey, buf, sizeof(buf), &ksym, NULL);
	if (n > 0)
		ttywrite(buf, (size_t)n, 1);
}

static void
aux_xinit(int cols, int rows)
{
	int w;
	int h;

	auxw.cw = MAX((int)(8 * cwscale), 1);
	auxw.ch = MAX((int)(16 * chscale), 1);
	w = 2 * borderpx + cols * auxw.cw;
	h = 2 * borderpx + rows * auxw.ch;

	auxw.dpy = XOpenDisplay(NULL);
	if (!auxw.dpy)
		die("cannot open display\n");
	auxw.scr = DefaultScreen(auxw.dpy);
	auxw.win = XCreateSimpleWindow(auxw.dpy, RootWindow(auxw.dpy, auxw.scr),
			0, 0, (unsigned int)w, (unsigned int)h, 0, 0, 0);
	auxw.gc = XCreateGC(auxw.dpy, auxw.win, 0, NULL);
	auxw.event_mask = KeyPressMask | ExposureMask | StructureNotifyMask | FocusChangeMask;
	XSelectInput(auxw.dpy, auxw.win, auxw.event_mask);
	auxw.wmdelete = XInternAtom(auxw.dpy, "WM_DELETE_WINDOW", False);
	XMapWindow(auxw.dpy, auxw.win);
	/* Request keyboard focus so x6 routes KeyPress events to our window. */
	XSetInputFocus(auxw.dpy, auxw.win, RevertToPointerRoot, CurrentTime);
	xloadcols();
	xsettitle(opt_title);
	aux_cresize(w, h);
}

static void
aux_run(void)
{
	XEvent ev;
	fd_set rfd;
	int ttyfd;
	int xfd;
	int maxfd;

	ttyfd = ttynew(opt_line, shell, opt_io, opt_cmd);
	xfd = ConnectionNumber(auxw.dpy);
	maxfd = (ttyfd > xfd) ? ttyfd : xfd;
	draw();

	for (;;) {
		FD_ZERO(&rfd);
		FD_SET(ttyfd, &rfd);
		FD_SET(xfd, &rfd);
		if (select(maxfd + 1, &rfd, NULL, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue;
			die("select failed: %s\n", strerror(errno));
		}

		if (FD_ISSET(ttyfd, &rfd))
			ttyread();

		while (XPending(auxw.dpy)) {
			XNextEvent(auxw.dpy, &ev);
			switch (ev.type) {
			case Expose:
				redraw();
				break;
			case ConfigureNotify:
				aux_cresize(ev.xconfigure.width, ev.xconfigure.height);
				break;
			case KeyPress:
				aux_kpress(&ev);
				break;
			case ClientMessage:
				ttyhangup();
				exit(0);
				break;
			default:
				break;
			}
		}
		draw();
	}
}

static void
usage(void)
{
	die("usage: %s [-v] [-t title] [[-e] command [args...]]\n", argv0);
}

int
main(int argc, char *argv[])
{
	int cols_local;
	int rows_local;

	ARGBEGIN {
	case 'e':
		if (argc > 0)
			--argc, ++argv;
		goto run;
	case 't':
	case 'T':
		opt_title = EARGF(usage());
		break;
	case 'l':
		opt_line = EARGF(usage());
		break;
	case 'o':
		opt_io = EARGF(usage());
		break;
	case 'v':
		die("%s " VERSION "\n", argv0);
		break;
	default:
		usage();
	} ARGEND;

run:
	if (argc > 0)
		opt_cmd = argv;

	if (!opt_title)
		opt_title = (opt_line || !opt_cmd) ? "st" : opt_cmd[0];

	setlocale(LC_CTYPE, "");
	cols_local = MAX((int)cols, 1);
	rows_local = MAX((int)rows, 1);
	tnew(cols_local, rows_local);
	selinit();
	aux_xinit(cols_local, rows_local);
	aux_run();
	return 0;
}
