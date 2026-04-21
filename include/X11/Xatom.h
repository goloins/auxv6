#ifndef _X11_XATOM_H_
#define _X11_XATOM_H_

#include <X11/Xlib.h>

/* Atom definitions */
#define XA_PRIMARY 1L
#define XA_SECONDARY 2L
#define XA_ATOM 4L
#define XA_CARDINAL 6L
#define XA_FONT 18L
#define XA_STRING 31L
#define XA_VISUALID 32L
#define XA_WINDOW 33L
#define XA_WM_HINTS 35L
#define XA_WM_NAME 39L
#define XA_WM_NORMAL_HINTS 40L
#define XA_WM_TRANSIENT_FOR 68L
#define XA_WM_COLORMAP_WINDOWS 71L
#define XA_WM_ICON_NAME 37L
#define XA_WM_CLASS 67L
#define XA_PIXMAP 20L
#define XA_BITMAP 5L
#define XA_INTEGER 19L
#define XA_RGB_COLOR_MAP 24L
#define XA_RESOURCE_MANAGER 23L

/* Special atom value */
#define AnyPropertyType 0L

#endif

