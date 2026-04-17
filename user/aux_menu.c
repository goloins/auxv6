/*
 * user/aux_menu.c — AUX Menu Protocol v1 app-side adapter helpers
 *
 * Implementation: standard XChangeProperty / XGetWindowProperty only.
 * No non-standard X11 calls.
 */

#include "auxv6/aux_menu.h"
#include "string.h"
#include "stdlib.h"

#ifndef XA_CARDINAL
#define XA_CARDINAL 6L
#endif

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static Atom
am_intern(Display *dpy, const char *name)
{
    return XInternAtom(dpy, (char *)name, False);
}

static Atom
am_utf8(Display *dpy)
{
    return am_intern(dpy, "UTF8_STRING");
}

/* Read current _AUX_MENU_SERIAL value; return 0 on miss. */
static unsigned long
am_read_serial(Display *dpy, Window w)
{
    Atom                actual;
    int                 fmt;
    unsigned long       n, ba;
    unsigned char      *prop = NULL;
    unsigned long       val = 0;

    if (XGetWindowProperty(dpy, w, am_intern(dpy, "_AUX_MENU_SERIAL"),
                           0, 1, False, XA_CARDINAL,
                           &actual, &fmt, &n, &ba, &prop) == Success && prop) {
        val = *(unsigned long *)prop;
        XFree(prop);
    }
    return val;
}

/* Write a serial value. */
static void
am_write_serial(Display *dpy, Window w, unsigned long serial)
{
    XChangeProperty(dpy, w, am_intern(dpy, "_AUX_MENU_SERIAL"),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&serial, 1);
}

/* Set or delete a UTF8_STRING property. */
static void
am_write_utf8(Display *dpy, Window w, const char *atom_name, const char *text)
{
    Atom prop = am_intern(dpy, atom_name);
    if (!text) {
        XDeleteProperty(dpy, w, prop);
        return;
    }
    XChangeProperty(dpy, w, prop, am_utf8(dpy), 8, PropModeReplace,
                    (unsigned char *)text, (int)strlen(text));
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void
aux_menu_publish(Display *dpy, Window w,
                 const char *model, const char *state, const char *caps)
{
    unsigned long ver = AUX_MENU_VERSION;

    if (!dpy || !w)
        return;

    /* Version */
    XChangeProperty(dpy, w, am_intern(dpy, "_AUX_MENU_VERSION"),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&ver, 1);

    /* Model and state (NULL clears the property) */
    am_write_utf8(dpy, w, "_AUX_MENU_MODEL", model);
    am_write_utf8(dpy, w, "_AUX_MENU_STATE", state);

    /* Caps only written if caller supplies a value; leave existing on NULL */
    if (caps)
        am_write_utf8(dpy, w, "_AUX_MENU_CAPS", caps);

    /* Increment serial last so menubar sees a consistent snapshot */
    am_write_serial(dpy, w, am_read_serial(dpy, w) + 1);
}

void
aux_menu_set_state(Display *dpy, Window w, const char *state)
{
    if (!dpy || !w)
        return;
    am_write_utf8(dpy, w, "_AUX_MENU_STATE", state);
    am_write_serial(dpy, w, am_read_serial(dpy, w) + 1);
}

int
aux_menu_is_command_event(Display *dpy, XClientMessageEvent *ev)
{
    if (!dpy || !ev)
        return 0;
    return (ev->message_type == am_intern(dpy, "_AUX_MENU_COMMAND") &&
            ev->data.l[0] == 1);
}

int
aux_menu_get_command(Display *dpy, Window w, char *buf, int bufsiz)
{
    Atom           prop = am_intern(dpy, "_AUX_MENU_COMMAND_TEXT");
    Atom           actual;
    int            fmt;
    unsigned long  n, ba;
    unsigned char *data = NULL;
    int            len;
    /* Request at most COMMAND_TEXT_MAX/4 + 1 longs (= COMMAND_TEXT_MAX bytes) */
    long           req_len = (AUX_MENU_COMMAND_TEXT_MAX / 4) + 1;

    if (!buf || bufsiz <= 0)
        return 0;
    buf[0] = '\0';

    if (XGetWindowProperty(dpy, w, prop, 0, req_len,
                           True /* delete after read */,
                           am_utf8(dpy),
                           &actual, &fmt, &n, &ba, &data) != Success || !data)
        return 0;

    len = (int)n;
    /* Hard cap: never write more than bufsiz-1 or COMMAND_TEXT_MAX-1 bytes */
    if (len >= AUX_MENU_COMMAND_TEXT_MAX)
        len = AUX_MENU_COMMAND_TEXT_MAX - 1;
    if (len >= bufsiz)
        len = bufsiz - 1;

    memmove(buf, data, (size_t)len);
    buf[len] = '\0';
    XFree(data);
    return len;
}
