#ifndef _X11_EXTENSIONS_XDAMAGE_H_
#define _X11_EXTENSIONS_XDAMAGE_H_

#include <X11/Xlib.h>

typedef XID Damage;

#define XDamageReportRawRectangles 0
#define XDamageReportDeltaRectangles 1
#define XDamageReportBoundingBox 2
#define XDamageReportNonEmpty 3

Status XDamageQueryExtension(Display *display,
                             int *event_base_return,
                             int *error_base_return);
Status XDamageQueryVersion(Display *display,
                           int *major_version_return,
                           int *minor_version_return);

Damage XDamageCreate(Display *display, Drawable drawable, int level);
void XDamageDestroy(Display *display, Damage damage);
void XDamageSubtract(Display *display, Damage damage,
                     Region repair, Region parts);

#endif
