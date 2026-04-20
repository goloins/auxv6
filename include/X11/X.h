#ifndef _X11_X_H_
#define _X11_X_H_

/*
 * auxv6 compatibility shim:
 * plan9port/rio expects core protocol constants from <X11/X.h>.
 * Our Xlib surface already carries these definitions.
 */
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

#endif
