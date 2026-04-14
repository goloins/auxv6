#ifndef _X11_EXTENSIONS_XCOMPOSITE_H_
#define _X11_EXTENSIONS_XCOMPOSITE_H_

#include <X11/Xlib.h>

#define CompositeRedirectAutomatic 0
#define CompositeRedirectManual 1

Status XCompositeQueryExtension(Display *display,
								int *event_base_return,
								int *error_base_return);
Status XCompositeQueryVersion(Display *display,
							  int *major_version_return,
							  int *minor_version_return);

void XCompositeRedirectWindow(Display *display, Window w, int update);
void XCompositeUnredirectWindow(Display *display, Window w, int update);
Pixmap XCompositeNameWindowPixmap(Display *display, Window w);

#endif
