#ifndef _X11_XATOM_H_
#define _X11_XATOM_H_

#include <X11/Xlib.h>

/* Atom definitions */
#define XA_PRIMARY 1L
#define XA_SECONDARY 2L
#define XA_ATOM 4L
#define XA_CARDINAL 6L
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

/* Special atom value */
#define AnyPropertyType 0L

/* Window property constants */
#define XValue 1
#define YValue 2
#define WidthValue 4
#define HeightValue 8
#define AllValues 15
#define XNegative 16
#define YNegative 32
#define USPosition 1
#define USSize 2
#define PPosition 4
#define PSize 8
#define PMinSize 16
#define PMaxSize 32
#define PResizeInc 64
#define PAspect 128
#define PBaseSize 256
#define PWinGravity 512

/* Gravity constants */
#define UnmapGravity 0
#define NorthWestGravity 1
#define NorthGravity 2
#define NorthEastGravity 3
#define WestGravity 4
#define CenterGravity 5
#define EastGravity 6
#define SouthWestGravity 7
#define SouthGravity 8
#define SouthEastGravity 9
#define StaticGravity 10

#endif

