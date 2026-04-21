#ifndef _X11_XMD_H_
#define _X11_XMD_H_

/* Minimal Xmd shim for ports that expect protocol integer typedefs. */

typedef unsigned char  CARD8;
typedef unsigned short CARD16;
typedef unsigned int   CARD32;
typedef signed char    INT8;
typedef short          INT16;
typedef int            INT32;

typedef CARD32 B32;
typedef CARD16 B16;

typedef CARD8  BYTE;
typedef CARD32 BITS32;

typedef unsigned long ULONG64;
typedef long          LONG64;

#endif
