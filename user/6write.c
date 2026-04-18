#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "unistd.h"
#include "errno.h"
#include "stdlib.h"
#include "string.h"

#include "X11/Xlib.h"
#include "X11/Xutil.h"
#include "X11/keysym.h"

#ifndef XA_CARDINAL
#define XA_CARDINAL 6L
#endif

enum {
  CMD_NEW = 1,
  CMD_SAVE,
  CMD_QUIT,
  CMD_SELECT_ALL,
};

typedef struct {
  char **lines;
  int nlines;
  int cap;
  int cx;
  int cy;
  int top;
  int dirty;
  int want_quit;
  int select_all;
  char path[256];
  char status[160];
} Editor;

typedef struct {
  Display *dpy;
  int screen;
  Window win;
  GC gc;
  XFontStruct *font;
  Atom wm_delete;
  Atom a_aux_menu_version;
  Atom a_aux_menu_serial;
  Atom a_aux_menu_model;
  Atom a_aux_menu_command;
  Atom a_aux_menu_command_text;
  unsigned long menu_serial;
  int width;
  int height;
  int char_w;
  int line_h;
  int ascent;
  int status_h;
  int gutter;
  unsigned long col_bg;
  unsigned long col_text;
  unsigned long col_status;
} App;

static int
clampi(int v, int lo, int hi)
{
  if(v < lo)
    return lo;
  if(v > hi)
    return hi;
  return v;
}

static char *
str_dup_n(const char *s, int n)
{
  char *out;

  if(!s)
    return 0;
  if(n < 0)
    n = strlen(s);
  out = malloc(n + 1);
  if(!out)
    return 0;
  memmove(out, s, n);
  out[n] = 0;
  return out;
}

static int
editor_reserve(Editor *ed, int need)
{
  char **nl;
  int ncap;

  if(need <= ed->cap)
    return 0;
  ncap = ed->cap ? ed->cap : 16;
  while(ncap < need)
    ncap *= 2;
  nl = realloc(ed->lines, ncap * sizeof(char *));
  if(!nl)
    return -1;
  ed->lines = nl;
  ed->cap = ncap;
  return 0;
}

static void
editor_set_status(Editor *ed, const char *msg)
{
  int n;

  if(!msg)
    msg = "";
  n = strlen(msg);
  if(n >= (int)sizeof(ed->status))
    n = sizeof(ed->status) - 1;
  memmove(ed->status, msg, n);
  ed->status[n] = 0;
}

static int
editor_insert_line(Editor *ed, int idx, char *line)
{
  int i;

  if(editor_reserve(ed, ed->nlines + 1) < 0)
    return -1;
  for(i = ed->nlines; i > idx; i--)
    ed->lines[i] = ed->lines[i - 1];
  ed->lines[idx] = line;
  ed->nlines++;
  return 0;
}

static int
editor_init(Editor *ed)
{
  memset(ed, 0, sizeof(*ed));
  if(editor_reserve(ed, 16) < 0)
    return -1;
  ed->lines[0] = str_dup_n("", 0);
  if(!ed->lines[0])
    return -1;
  ed->nlines = 1;
  editor_set_status(ed, "Ctrl+S Save  Ctrl+Q Quit  Click menu for commands");
  return 0;
}

static void
editor_free(Editor *ed)
{
  int i;
  for(i = 0; i < ed->nlines; i++)
    free(ed->lines[i]);
  free(ed->lines);
  memset(ed, 0, sizeof(*ed));
}

static void
editor_clamp_cursor(Editor *ed)
{
  int len;

  if(ed->nlines <= 0)
    return;
  ed->cy = clampi(ed->cy, 0, ed->nlines - 1);
  len = strlen(ed->lines[ed->cy]);
  ed->cx = clampi(ed->cx, 0, len);
}

static void
editor_ensure_visible(Editor *ed, App *app)
{
  int vis;

  vis = (app->height - app->status_h - app->gutter * 2) / app->line_h;
  if(vis < 1)
    vis = 1;
  if(ed->cy < ed->top)
    ed->top = ed->cy;
  if(ed->cy >= ed->top + vis)
    ed->top = ed->cy - vis + 1;
  if(ed->top < 0)
    ed->top = 0;
}

static int
editor_load(Editor *ed, const char *path)
{
  int fd;
  char ch;
  char *line;
  int lcap;
  int llen;
  int nr;

  if(!path || !path[0])
    return -1;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;

  while(ed->nlines > 0) {
    free(ed->lines[ed->nlines - 1]);
    ed->nlines--;
  }

  line = malloc(128);
  if(!line) {
    close(fd);
    return -1;
  }
  lcap = 128;
  llen = 0;

  while((nr = read(fd, &ch, 1)) == 1) {
    if(ch == '\r')
      continue;
    if(ch == '\n') {
      char *s = str_dup_n(line, llen);
      if(!s || editor_insert_line(ed, ed->nlines, s) < 0) {
        free(s);
        free(line);
        close(fd);
        return -1;
      }
      llen = 0;
      continue;
    }
    if(llen + 1 >= lcap) {
      char *nl;
      lcap *= 2;
      nl = realloc(line, lcap);
      if(!nl) {
        free(line);
        close(fd);
        return -1;
      }
      line = nl;
    }
    line[llen++] = ch;
  }

  if(llen > 0 || ed->nlines == 0) {
    char *s = str_dup_n(line, llen);
    if(!s || editor_insert_line(ed, ed->nlines, s) < 0) {
      free(s);
      free(line);
      close(fd);
      return -1;
    }
  }

  free(line);
  close(fd);

  ed->cx = ed->cy = ed->top = 0;
  ed->dirty = 0;
  ed->select_all = 0;

  memset(ed->path, 0, sizeof(ed->path));
  strncpy(ed->path, path, sizeof(ed->path) - 1);
  editor_set_status(ed, "Opened file");
  return 0;
}

static int
editor_save(Editor *ed)
{
  int fd;
  int i;

  if(!ed->path[0]) {
    strncpy(ed->path, "6write.txt", sizeof(ed->path) - 1);
    ed->path[sizeof(ed->path) - 1] = 0;
  }

  fd = open(ed->path, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0) {
    editor_set_status(ed, "Save failed: cannot open output path");
    return -1;
  }

  for(i = 0; i < ed->nlines; i++) {
    int len = strlen(ed->lines[i]);
    if(len > 0 && write(fd, ed->lines[i], len) != len) {
      close(fd);
      editor_set_status(ed, "Save failed: write error");
      return -1;
    }
    if(i < ed->nlines - 1 && write(fd, "\n", 1) != 1) {
      close(fd);
      editor_set_status(ed, "Save failed: write error");
      return -1;
    }
  }

  close(fd);
  ed->dirty = 0;
  editor_set_status(ed, "Saved");
  return 0;
}

static void
editor_new(Editor *ed)
{
  int i;

  for(i = 0; i < ed->nlines; i++)
    free(ed->lines[i]);
  ed->nlines = 0;
  ed->lines[0] = str_dup_n("", 0);
  ed->nlines = 1;
  ed->cx = ed->cy = ed->top = 0;
  ed->dirty = 0;
  ed->select_all = 0;
  memset(ed->path, 0, sizeof(ed->path));
  editor_set_status(ed, "New buffer");
}

static int
editor_insert_char(Editor *ed, int ch)
{
  char *s;
  int len;
  char *n;

  if(ch < 32 || ch == 127)
    return 0;

  s = ed->lines[ed->cy];
  len = strlen(s);
  n = malloc(len + 2);
  if(!n)
    return -1;
  memmove(n, s, ed->cx);
  n[ed->cx] = ch;
  memmove(n + ed->cx + 1, s + ed->cx, len - ed->cx);
  n[len + 1] = 0;
  free(s);
  ed->lines[ed->cy] = n;
  ed->cx++;
  ed->dirty = 1;
  ed->select_all = 0;
  return 0;
}

static int
editor_backspace(Editor *ed)
{
  char *s;
  int len;

  if(ed->cx > 0) {
    s = ed->lines[ed->cy];
    len = strlen(s);
    memmove(s + ed->cx - 1, s + ed->cx, len - ed->cx + 1);
    ed->cx--;
    ed->dirty = 1;
    ed->select_all = 0;
    return 0;
  }

  if(ed->cy > 0) {
    char *cur = ed->lines[ed->cy];
    char *prev = ed->lines[ed->cy - 1];
    int prev_len = strlen(prev);
    int cur_len = strlen(cur);
    char *n = malloc(prev_len + cur_len + 1);
    int i;

    if(!n)
      return -1;
    memmove(n, prev, prev_len);
    memmove(n + prev_len, cur, cur_len + 1);
    free(prev);
    free(cur);
    ed->lines[ed->cy - 1] = n;
    for(i = ed->cy; i < ed->nlines - 1; i++)
      ed->lines[i] = ed->lines[i + 1];
    ed->nlines--;
    ed->cy--;
    ed->cx = prev_len;
    ed->dirty = 1;
    ed->select_all = 0;
  }

  return 0;
}

static int
editor_delete(Editor *ed)
{
  char *s;
  int len;

  s = ed->lines[ed->cy];
  len = strlen(s);
  if(ed->cx < len) {
    memmove(s + ed->cx, s + ed->cx + 1, len - ed->cx);
    ed->dirty = 1;
    ed->select_all = 0;
    return 0;
  }

  if(ed->cy < ed->nlines - 1) {
    char *next = ed->lines[ed->cy + 1];
    int next_len = strlen(next);
    char *n = malloc(len + next_len + 1);
    int i;

    if(!n)
      return -1;
    memmove(n, s, len);
    memmove(n + len, next, next_len + 1);
    free(s);
    free(next);
    ed->lines[ed->cy] = n;
    for(i = ed->cy + 1; i < ed->nlines - 1; i++)
      ed->lines[i] = ed->lines[i + 1];
    ed->nlines--;
    ed->dirty = 1;
    ed->select_all = 0;
  }

  return 0;
}

static int
editor_newline(Editor *ed)
{
  char *s;
  int len;
  char *left;
  char *right;

  s = ed->lines[ed->cy];
  len = strlen(s);
  left = str_dup_n(s, ed->cx);
  right = str_dup_n(s + ed->cx, len - ed->cx);
  if(!left || !right) {
    free(left);
    free(right);
    return -1;
  }

  free(s);
  ed->lines[ed->cy] = left;
  if(editor_insert_line(ed, ed->cy + 1, right) < 0) {
    free(right);
    return -1;
  }
  ed->cy++;
  ed->cx = 0;
  ed->dirty = 1;
  ed->select_all = 0;
  return 0;
}

static void
editor_update_title(App *app, Editor *ed)
{
  char title[320];
  const char *name;

  name = ed->path[0] ? ed->path : "Untitled";
  snprintf(title, sizeof(title), "6write - %s%s", name, ed->dirty ? " *" : "");
  XStoreName(app->dpy, app->win, title);
}

static int
alloc_named_color(App *app, const char *name, unsigned long fallback)
{
  XColor c;

  if(XParseColor(app->dpy, DefaultColormap(app->dpy, app->screen), name, &c) == 0)
    return fallback;
  if(XAllocColor(app->dpy, DefaultColormap(app->dpy, app->screen), &c) == 0)
    return fallback;
  return c.pixel;
}

static void
draw_status(App *app, Editor *ed)
{
  int y = app->height - app->status_h;
  char st[256];
  int n;

  XSetForeground(app->dpy, app->gc, app->col_status);
  XFillRectangle(app->dpy, app->win, app->gc, 0, y, app->width, app->status_h);

  XSetForeground(app->dpy, app->gc, app->col_text);
  XDrawLine(app->dpy, app->win, app->gc, 0, y, app->width, y);

  n = snprintf(st, sizeof(st), "%s | Ln %d, Col %d", ed->status, ed->cy + 1, ed->cx + 1);
  if(n < 0)
    n = 0;
  if(n >= (int)sizeof(st))
    n = sizeof(st) - 1;

  XSetForeground(app->dpy, app->gc, app->col_text);
  XDrawString(app->dpy, app->win, app->gc, app->gutter, y + app->ascent + 2, st, n);
}

static void
draw_text(App *app, Editor *ed)
{
  int text_y0;
  int text_h;
  int vis;
  int i;

  text_y0 = 0;
  text_h = app->height - app->status_h;
  if(text_h < 1)
    text_h = 1;

  XSetForeground(app->dpy, app->gc, app->col_bg);
  XFillRectangle(app->dpy, app->win, app->gc, 0, text_y0, app->width, text_h);

  vis = text_h / app->line_h;
  if(vis < 1)
    vis = 1;

  for(i = 0; i < vis; i++) {
    int line_idx = ed->top + i;
    int y = text_y0 + i * app->line_h + app->ascent + 2;
    if(line_idx >= ed->nlines)
      break;
    XSetForeground(app->dpy, app->gc, app->col_text);
    XDrawString(app->dpy, app->win, app->gc, app->gutter, y,
                ed->lines[line_idx], strlen(ed->lines[line_idx]));
  }

  if(ed->cy >= ed->top && ed->cy < ed->top + vis) {
    int cx = app->gutter + ed->cx * app->char_w;
    int cy = text_y0 + (ed->cy - ed->top) * app->line_h;
    if(ed->select_all) {
      XSetForeground(app->dpy, app->gc, app->col_bg);
      XFillRectangle(app->dpy, app->win, app->gc,
                     app->gutter,
                     text_y0,
                     app->width - app->gutter * 2,
                     vis * app->line_h);
      for(i = 0; i < vis; i++) {
        int line_idx = ed->top + i;
        int y = text_y0 + i * app->line_h + app->ascent + 2;
        if(line_idx >= ed->nlines)
          break;
        XSetForeground(app->dpy, app->gc, app->col_text);
        XDrawString(app->dpy, app->win, app->gc, app->gutter, y,
                    ed->lines[line_idx], strlen(ed->lines[line_idx]));
      }
    }
    XSetForeground(app->dpy, app->gc, app->col_text);
    XFillRectangle(app->dpy, app->win, app->gc, cx, cy + 2, 2, app->line_h - 4);
  }
}

static void
draw(App *app, Editor *ed)
{
  editor_clamp_cursor(ed);
  editor_ensure_visible(ed, app);
  editor_update_title(app, ed);

  draw_text(app, ed);
  draw_status(app, ed);
  XFlush(app->dpy);
}

static void
run_command(App *app, Editor *ed, int cmd)
{
  switch(cmd) {
  case CMD_NEW:
    if(ed->dirty)
      editor_set_status(ed, "Discarded unsaved changes");
    editor_new(ed);
    break;
  case CMD_SAVE:
    editor_save(ed);
    break;
  case CMD_QUIT:
    if(ed->dirty && !ed->want_quit) {
      ed->want_quit = 1;
      editor_set_status(ed, "Unsaved changes. Invoke Quit again to exit.");
    } else {
      ed->want_quit = 2;
    }
    break;
  case CMD_SELECT_ALL:
    ed->select_all = 1;
    editor_set_status(ed, "Select All (visual only)");
    break;
  }
}

static void
handle_key(App *app, Editor *ed, XKeyEvent *kev)
{
  KeySym ks = 0;
  char buf[16];
  int n;
  int ctrl;

  n = XLookupString(kev, buf, sizeof(buf), &ks, 0);
  ctrl = (kev->state & ControlMask) != 0;

  ed->want_quit = 0;

  if(ctrl && n > 0 && (buf[0] == 'q' || buf[0] == 'Q')) {
    run_command(app, ed, CMD_QUIT);
    return;
  }
  if(ctrl && n > 0 && (buf[0] == 's' || buf[0] == 'S')) {
    run_command(app, ed, CMD_SAVE);
    return;
  }
  if(ctrl && n > 0 && (buf[0] == 'n' || buf[0] == 'N')) {
    run_command(app, ed, CMD_NEW);
    return;
  }
  if(ctrl && n > 0 && (buf[0] == 'a' || buf[0] == 'A')) {
    run_command(app, ed, CMD_SELECT_ALL);
    return;
  }

  switch(ks) {
  case XK_Left:
    if(ed->cx > 0)
      ed->cx--;
    else if(ed->cy > 0) {
      ed->cy--;
      ed->cx = strlen(ed->lines[ed->cy]);
    }
    ed->select_all = 0;
    return;
  case XK_Right: {
    int len = strlen(ed->lines[ed->cy]);
    if(ed->cx < len)
      ed->cx++;
    else if(ed->cy < ed->nlines - 1) {
      ed->cy++;
      ed->cx = 0;
    }
    ed->select_all = 0;
    return;
  }
  case XK_Up:
    if(ed->cy > 0)
      ed->cy--;
    ed->cx = clampi(ed->cx, 0, strlen(ed->lines[ed->cy]));
    ed->select_all = 0;
    return;
  case XK_Down:
    if(ed->cy < ed->nlines - 1)
      ed->cy++;
    ed->cx = clampi(ed->cx, 0, strlen(ed->lines[ed->cy]));
    ed->select_all = 0;
    return;
  case XK_Home:
    ed->cx = 0;
    ed->select_all = 0;
    return;
  case XK_End:
    ed->cx = strlen(ed->lines[ed->cy]);
    ed->select_all = 0;
    return;
  case XK_Prior:
    ed->cy -= 10;
    if(ed->cy < 0)
      ed->cy = 0;
    ed->cx = clampi(ed->cx, 0, strlen(ed->lines[ed->cy]));
    ed->select_all = 0;
    return;
  case XK_Next:
    ed->cy += 10;
    if(ed->cy >= ed->nlines)
      ed->cy = ed->nlines - 1;
    ed->cx = clampi(ed->cx, 0, strlen(ed->lines[ed->cy]));
    ed->select_all = 0;
    return;
  case XK_Return:
  case XK_KP_Enter:
    editor_newline(ed);
    return;
  case XK_BackSpace:
    editor_backspace(ed);
    return;
  case XK_Delete:
    editor_delete(ed);
    return;
  case XK_Tab:
    editor_insert_char(ed, ' ');
    editor_insert_char(ed, ' ');
    editor_insert_char(ed, ' ');
    editor_insert_char(ed, ' ');
    return;
  }

  if(n > 0) {
    int i;
    for(i = 0; i < n; i++) {
      if((unsigned char)buf[i] >= 32 && (unsigned char)buf[i] != 127)
        editor_insert_char(ed, (unsigned char)buf[i]);
    }
  }
}

static void
handle_button(App *app, Editor *ed, XButtonEvent *bev)
{
  int x = bev->x;
  int y = bev->y;

  if(bev->button == Button4) {
    ed->cy -= 3;
    if(ed->cy < 0)
      ed->cy = 0;
    ed->cx = clampi(ed->cx, 0, strlen(ed->lines[ed->cy]));
    ed->select_all = 0;
    return;
  }
  if(bev->button == Button5) {
    ed->cy += 3;
    if(ed->cy >= ed->nlines)
      ed->cy = ed->nlines - 1;
    ed->cx = clampi(ed->cx, 0, strlen(ed->lines[ed->cy]));
    ed->select_all = 0;
    return;
  }

  if(y >= 0 && y < app->height - app->status_h) {
    int row = y / app->line_h;
    int col = (x - app->gutter + app->char_w / 2) / app->char_w;
    int line_idx = ed->top + row;
    int len;

    if(line_idx < 0)
      line_idx = 0;
    if(line_idx >= ed->nlines)
      line_idx = ed->nlines - 1;

    len = strlen(ed->lines[line_idx]);
    if(col < 0)
      col = 0;
    if(col > len)
      col = len;

    ed->cy = line_idx;
    ed->cx = col;
    ed->select_all = 0;
  }
}

static void
publish_menu_model(App *app)
{
  static const char model[] =
    "MENU|m_file|root|File|0\n"
    "ITEM|i_new|m_file|New|cmd.new|normal\n"
    "ITEM|i_save|m_file|Save|cmd.save|normal\n"
    "ITEM|i_quit|m_file|Quit|cmd.quit|normal\n"
    "MENU|m_edit|root|Edit|1\n"
    "ITEM|i_selall|m_edit|Select All|cmd.select_all|normal\n";
  unsigned long ver = 1;

  app->menu_serial++;

  XChangeProperty(app->dpy, app->win,
                  app->a_aux_menu_version,
                  XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&ver, 1);
  XChangeProperty(app->dpy, app->win,
                  app->a_aux_menu_model,
                  XInternAtom(app->dpy, "UTF8_STRING", False),
                  8, PropModeReplace,
                  (unsigned char *)model, (int)strlen(model));
  XChangeProperty(app->dpy, app->win,
                  app->a_aux_menu_serial,
                  XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&app->menu_serial, 1);
}

static void
dispatch_aux_command(App *app, Editor *ed)
{
  Atom at; int fmt; unsigned long n, ba; unsigned char *data = NULL;
  int cmd = 0;

  if(XGetWindowProperty(app->dpy, app->win,
                        app->a_aux_menu_command_text,
                        0, 256, False, AnyPropertyType,
                        &at, &fmt, &n, &ba, &data) != Success || !data)
    return;

  if(n > 0) {
    char buf[256];
    int len = (n < sizeof(buf) - 1) ? (int)n : (int)(sizeof(buf) - 1);
    memmove(buf, data, len);
    buf[len] = '\0';

    if(strcmp(buf, "cmd.new") == 0)         cmd = CMD_NEW;
    else if(strcmp(buf, "cmd.save") == 0)   cmd = CMD_SAVE;
    else if(strcmp(buf, "cmd.quit") == 0)   cmd = CMD_QUIT;
    else if(strcmp(buf, "cmd.select_all") == 0) cmd = CMD_SELECT_ALL;
  }
  XFree(data);
  XDeleteProperty(app->dpy, app->win, app->a_aux_menu_command_text);

  if(cmd)
    run_command(app, ed, cmd);
}

int
main(int argc, char **argv)
{
  App app;
  Editor ed;
  XEvent ev;

  memset(&app, 0, sizeof(app));
  if(editor_init(&ed) < 0) {
    dprintf(2, "6write: out of memory\n");
    return 1;
  }

  app.dpy = XOpenDisplay(0);
  if(!app.dpy) {
    dprintf(2, "6write: cannot open display\n");
    editor_free(&ed);
    return 1;
  }

  app.screen = XDefaultScreen(app.dpy);
  app.width = 820;
  app.height = 560;
  app.status_h = 18;
  app.gutter = 6;

  app.win = XCreateSimpleWindow(app.dpy,
                                XRootWindow(app.dpy, app.screen),
                                60, 60,
                                app.width, app.height,
                                1,
                                XBlackPixel(app.dpy, app.screen),
                                XWhitePixel(app.dpy, app.screen));

  XSelectInput(app.dpy, app.win,
               ExposureMask | KeyPressMask | StructureNotifyMask |
               ButtonPressMask | PropertyChangeMask);

  app.wm_delete              = XInternAtom(app.dpy, "WM_DELETE_WINDOW",     False);
  app.a_aux_menu_version      = XInternAtom(app.dpy, "_AUX_MENU_VERSION",    False);
  app.a_aux_menu_serial       = XInternAtom(app.dpy, "_AUX_MENU_SERIAL",     False);
  app.a_aux_menu_model        = XInternAtom(app.dpy, "_AUX_MENU_MODEL",      False);
  app.a_aux_menu_command      = XInternAtom(app.dpy, "_AUX_MENU_COMMAND",    False);
  app.a_aux_menu_command_text = XInternAtom(app.dpy, "_AUX_MENU_COMMAND_TEXT", False);
  XSetWMProtocols(app.dpy, app.win, &app.wm_delete, 1);

  app.gc = XCreateGC(app.dpy, app.win, 0, 0);
  app.font = XLoadQueryFont(app.dpy, "fixed");
  if(!app.font)
    app.font = XLoadQueryFont(app.dpy, "6x13");
  if(app.font) {
    XSetFont(app.dpy, app.gc, app.font->fid);
    app.char_w = XTextWidth(app.font, "M", 1);
    app.line_h = app.font->ascent + app.font->descent + 2;
    app.ascent = app.font->ascent;
  } else {
    app.char_w = 8;
    app.line_h = 16;
    app.ascent = 12;
  }

  app.col_bg     = alloc_named_color(&app, "gray95", XWhitePixel(app.dpy, app.screen));
  app.col_text   = alloc_named_color(&app, "black",  XBlackPixel(app.dpy, app.screen));
  app.col_status = alloc_named_color(&app, "gray88", XWhitePixel(app.dpy, app.screen));

  if(argc > 1) {
    if(editor_load(&ed, argv[1]) < 0) {
      strncpy(ed.path, argv[1], sizeof(ed.path) - 1);
      ed.path[sizeof(ed.path) - 1] = 0;
      editor_set_status(&ed, "Open failed; editing new buffer");
    }
  }

  XMapWindow(app.dpy, app.win);
  publish_menu_model(&app);

  while(ed.want_quit != 2) {
    XNextEvent(app.dpy, &ev);

    if(ev.type == Expose) {
      draw(&app, &ed);
      continue;
    }

    if(ev.type == ConfigureNotify) {
      app.width = ev.xconfigure.width;
      app.height = ev.xconfigure.height;
      draw(&app, &ed);
      continue;
    }

    if(ev.type == ButtonPress) {
      handle_button(&app, &ed, &ev.xbutton);
      draw(&app, &ed);
      continue;
    }

    if(ev.type == KeyPress) {
      handle_key(&app, &ed, &ev.xkey);
      if(ed.want_quit == 2)
        break;
      draw(&app, &ed);
      continue;
    }

    if(ev.type == ClientMessage) {
      if((Atom)ev.xclient.message_type == app.a_aux_menu_command) {
        dispatch_aux_command(&app, &ed);
        if(ed.want_quit == 2)
          break;
        draw(&app, &ed);
        continue;
      }
      if((Atom)ev.xclient.data.l[0] == app.wm_delete) {
        if(ed.dirty && !ed.want_quit) {
          ed.want_quit = 1;
          editor_set_status(&ed, "Unsaved changes. Close again to exit.");
          draw(&app, &ed);
        } else {
          break;
        }
      }
      continue;
    }
  }

  if(app.font)
    XFreeFont(app.dpy, app.font);
  XFreeGC(app.dpy, app.gc);
  XDestroyWindow(app.dpy, app.win);
  XCloseDisplay(app.dpy);
  editor_free(&ed);
  return 0;
}
