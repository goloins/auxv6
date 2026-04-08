#ifndef _X11_EXTENSIONS_XCOMPOSITE_H_
#define _X11_EXTENSIONS_XCOMPOSITE_H_

#include <X11/Xlib.h>

#define CompositeRedirectAutomatic 0
#define CompositeRedirectManual 1

void XCompositeRedirectWindow(Display *display, Window w, int update);
void XCompositeUnredirectWindow(Display *display, Window w, int update);

#endif
