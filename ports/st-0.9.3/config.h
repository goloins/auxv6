/* AUXV6_ST_HACK: reduced config surface for auxv6 backend.
 *
 * This intentionally excludes upstream shortcut/keymap tables that depend on
 * large X11 keysym coverage not yet present in auxv6 headers.
 */

static char *font = "fixed:pixelsize=16";
static int borderpx = 2;

static char *shell = "/bin/dash";
char *utmp = NULL;
char *scroll = NULL;
char *stty_args = "stty raw pass8 nl -echo -iexten -cstopb 38400";

char *vtiden = "\033[?6c";

static float cwscale = 1.0;
static float chscale = 1.0;

wchar_t *worddelimiters = L" ";

int allowaltscreen = 1;
int allowwindowops = 0;

static unsigned int doubleclicktimeout = 300;
static unsigned int tripleclicktimeout = 600;
static double minlatency = 2;
static double maxlatency = 33;
static unsigned int blinktimeout = 800;
static unsigned int cursorthickness = 2;
static int bellvolume = 0;

char *termname = "st-256color";
unsigned int tabspaces = 8;

static const char *colorname[] = {
	"black",
	"red3",
	"green3",
	"yellow3",
	"blue2",
	"magenta3",
	"cyan3",
	"gray90",
	"gray50",
	"red",
	"green",
	"yellow",
	"#5c5cff",
	"magenta",
	"cyan",
	"white",
	[255] = 0,
	"#cccccc",
	"#555555",
	"gray90",
	"black",
};

unsigned int defaultfg = 258;
unsigned int defaultbg = 259;
unsigned int defaultcs = 256;
static unsigned int defaultrcs = 257;

static unsigned int cursorshape = 2;
static unsigned int mouseshape = XC_xterm;
static unsigned int mousefg = 7;
static unsigned int mousebg = 0;
static unsigned int defaultattr = 11;

static uint forcemousemod = ShiftMask;

#define MODKEY Mod1Mask
#define TERMMOD (ControlMask|ShiftMask)

/* Keep auxv6 key/mouse surface minimal while satisfying upstream x.c tables. */
static MouseShortcut mshortcuts[] = {
	{ XK_ANY_MOD, Button2, selpaste, {.i = 0}, 1 },
};

static Shortcut shortcuts[] = {
	{ TERMMOD, XK_C, clipcopy, {.i = 0} },
	{ TERMMOD, XK_V, clippaste, {.i = 0} },
	{ TERMMOD, XK_Y, selpaste, {.i = 0} },
	{ ShiftMask, XK_Insert, selpaste, {.i = 0} },
};

static KeySym mappedkeys[] = { -1 };
static uint ignoremod = Mod2Mask|XK_SWITCH_MOD;

static Key key[] = {
	{ XK_Return, XK_ANY_MOD, "\r", 0, 0 },
	{ XK_Up, XK_ANY_MOD, "\033[A", 0, -1 },
	{ XK_Down, XK_ANY_MOD, "\033[B", 0, -1 },
	{ XK_Right, XK_ANY_MOD, "\033[C", 0, -1 },
	{ XK_Left, XK_ANY_MOD, "\033[D", 0, -1 },
	{ XK_BackSpace, XK_NO_MOD, "\177", 0, 0 },
};

static uint selmasks[] = {
	[SEL_RECTANGULAR] = Mod1Mask,
};

static char ascii_printable[] =
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~";

static unsigned int cols = 80;
static unsigned int rows = 24;
