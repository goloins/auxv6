/* AUXV6_ST_HACK: reduced config surface for auxv6 backend.
 *
 * This intentionally excludes upstream shortcut/keymap tables that depend on
 * large X11 keysym coverage not yet present in auxv6 headers.
 */

static char *font = "fixed:pixelsize=16";
static int borderpx = 2;

static char *shell = "/bin/sh";
char *utmp = NULL;
char *scroll = NULL;
char *stty_args = "stty raw pass8 nl -echo -iexten -cstopb 38400";

char *vtiden = "\033[?6c";

static float cwscale = 1.0;
static float chscale = 1.0;

wchar_t *worddelimiters = L" ";

int allowaltscreen = 1;
int allowwindowops = 0;

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

static unsigned int cols = 80;
static unsigned int rows = 24;
