/*
 * XDR (External Data Representation) - RFC 1014 implementation
 * Machine-independent data serialization for RPC
 */

#include "types.h"
#include "defs.h"
#include "net.h"
#include "xdr.h"
#include "x86.h"
#include "stddef.h"

/* Forward declare memcpy, memset */
extern void *memcpy(void*, void*, uint);
extern void *memset(void*, int, uint);
extern int strlen(const char*);

/*
 * XDR stream management
 */

void
xdr_init(XDR *xdrs, char *buf, uint len, xdr_op op)
{
  xdrs->buf = buf;
  xdrs->start = buf;
  xdrs->buflen = len;
  xdrs->pos = 0;
  xdrs->op = op;
}

void
xdr_reset(XDR *xdrs)
{
  xdrs->pos = 0;
  xdrs->buf = xdrs->start;
}

uint
xdr_getpos(XDR *xdrs)
{
  return xdrs->pos;
}

int
xdr_setpos(XDR *xdrs, uint pos)
{
  if (pos > xdrs->buflen)
    return 0;
  xdrs->pos = pos;
  xdrs->buf = xdrs->start + pos;
  return 1;
}

/*
 * Helper: ensure buffer has at least 'needed' bytes available
 */
static int
xdr_check_bytes(XDR *xdrs, uint needed)
{
  if (xdrs->pos + needed > xdrs->buflen)
    return 0;
  return 1;
}

/*
 * Helper: advance position and return current position
 */
static char*
xdr_advance(XDR *xdrs, uint bytes)
{
  char *cur = xdrs->buf;
  xdrs->buf += bytes;
  xdrs->pos += bytes;
  return cur;
}

/*
 * Helper: encode/decode 32-bit value with padding to 4-byte boundary
 */
static int
xdr_int_internal(XDR *xdrs, uint *ip)
{
  uint *p;

  if (!xdr_check_bytes(xdrs, 4))
    return 0;

  p = (uint *)xdr_advance(xdrs, 4);

  if (xdrs->op == XDR_ENCODE) {
    *p = net_htonl(*ip);
  } else if (xdrs->op == XDR_DECODE) {
    *ip = net_ntohl(*p);
  }
  return 1;
}

/*
 * 32-bit signed integer
 */
int
xdr_int(XDR *xdrs, int *ip)
{
  return xdr_int_internal(xdrs, (uint *)ip);
}

/*
 * 32-bit unsigned integer
 */
int
xdr_uint(XDR *xdrs, uint *up)
{
  return xdr_int_internal(xdrs, up);
}

/*
 * 32-bit signed long
 */
int
xdr_long(XDR *xdrs, long *lp)
{
  return xdr_int_internal(xdrs, (uint *)lp);
}

/*
 * 32-bit unsigned long
 */
int
xdr_ulong(XDR *xdrs, ulong *ulp)
{
  return xdr_int_internal(xdrs, (uint *)ulp);
}

/*
 * Short: encode as 32-bit, decode high 16 bits
 */
int
xdr_short(XDR *xdrs, short *sp)
{
  int val;
  
  if (xdrs->op == XDR_ENCODE) {
    val = *sp;
    return xdr_int_internal(xdrs, (uint *)&val);
  } else if (xdrs->op == XDR_DECODE) {
    if (!xdr_int_internal(xdrs, (uint *)&val))
      return 0;
    *sp = (short)(val >> 16);
    return 1;
  }
  return 0;
}

/*
 * Unsigned short: encode as 32-bit, decode high 16 bits
 */
int
xdr_ushort(XDR *xdrs, ushort *usp)
{
  uint val;
  
  if (xdrs->op == XDR_ENCODE) {
    val = *usp;
    return xdr_int_internal(xdrs, &val);
  } else if (xdrs->op == XDR_DECODE) {
    if (!xdr_int_internal(xdrs, &val))
      return 0;
    *usp = (ushort)(val >> 16);
    return 1;
  }
  return 0;
}

/*
 * Boolean (0 = false, != 0 = true)
 */
int
xdr_bool(XDR *xdrs, int *bp)
{
  uint val;
  
  if (xdrs->op == XDR_ENCODE) {
    val = *bp ? 1 : 0;
    return xdr_int_internal(xdrs, &val);
  } else if (xdrs->op == XDR_DECODE) {
    if (!xdr_int_internal(xdrs, &val))
      return 0;
    *bp = val ? 1 : 0;
    return 1;
  }
  return 0;
}

/*
 * Enumeration (32-bit value)
 */
int
xdr_enum(XDR *xdrs, int *ep)
{
  return xdr_int(xdrs, ep);
}

/*
 * Float (32-bit IEEE)
 */
int
xdr_float(XDR *xdrs, float *fp)
{
  uint *ip = (uint *)fp;
  return xdr_int_internal(xdrs, ip);
}

/*
 * Double (64-bit IEEE)
 */
int
xdr_double(XDR *xdrs, double *dp)
{
  uint *ip = (uint *)dp;
  
  if (!xdr_int_internal(xdrs, ip))
    return 0;
  if (!xdr_int_internal(xdrs, ip + 1))
    return 0;
  return 1;
}

/*
 * Opaque data: raw bytes without length prefix
 * Used for fixed-size byte sequences
 */
int
xdr_opaque(XDR *xdrs, char *cp, uint cnt)
{
  uint rndup = (cnt + 3) & ~3;  /* Round up to 4-byte boundary */

  if (!xdr_check_bytes(xdrs, rndup))
    return 0;

  char *p = xdr_advance(xdrs, rndup);

  if (xdrs->op == XDR_ENCODE) {
    memcpy(p, cp, cnt);
    memset(p + cnt, 0, rndup - cnt);  /* Zero-pad */
  } else if (xdrs->op == XDR_DECODE) {
    memcpy(cp, p, cnt);
  }
  return 1;
}

/*
 * Variable-length bytes: 4-byte length + data (padded to 4-byte boundary)
 */
int
xdr_bytes(XDR *xdrs, char **cpp, uint *sizep, uint maxsize)
{
  uint len = 0;

  if (xdrs->op == XDR_ENCODE) {
    len = *sizep;
    if (len > maxsize)
      return 0;
  }

  /* Encode/decode length */
  if (!xdr_uint(xdrs, &len))
    return 0;

  if (xdrs->op == XDR_DECODE) {
    if (len > maxsize)
      return 0;
    *sizep = len;
  }

  /* Encode/decode data with padding */
  uint rndup = (len + 3) & ~3;
  if (!xdr_check_bytes(xdrs, rndup))
    return 0;

  char *p = xdr_advance(xdrs, rndup);

  if (xdrs->op == XDR_ENCODE) {
    memcpy(p, *cpp, len);
    memset(p + len, 0, rndup - len);
  } else if (xdrs->op == XDR_DECODE) {
    *cpp = p;
  }
  return 1;
}

/*
 * String: variable-length with null terminator
 * 4-byte length + data + null (padded to 4-byte boundary)
 */
int
xdr_string(XDR *xdrs, char **cpp, uint maxsize)
{
  uint len = 0;

  if (xdrs->op == XDR_ENCODE) {
    len = strlen(*cpp);
    if (len >= maxsize)
      return 0;
    len++;  /* Include null terminator in count */
  }

  /* Encode/decode length */
  if (!xdr_uint(xdrs, &len))
    return 0;

  if (xdrs->op == XDR_DECODE) {
    if (len > maxsize || len == 0)
      return 0;
  }

  /* Encode/decode data with padding */
  uint rndup = (len + 3) & ~3;
  if (!xdr_check_bytes(xdrs, rndup))
    return 0;

  char *p = xdr_advance(xdrs, rndup);

  if (xdrs->op == XDR_ENCODE) {
    memcpy(p, *cpp, len);
    memset(p + len, 0, rndup - len);
  } else if (xdrs->op == XDR_DECODE) {
    *cpp = p;
  }
  return 1;
}

/*
 * Array: 4-byte count + variable-length elements
 * Each element is encoded using the provided function pointer
 */
int
xdr_array(XDR *xdrs, char **addrp, uint *sizep, uint maxsize, uint elsize,
          int (*elproc)(XDR *, char *))
{
  uint i, len = 0;

  if (xdrs->op == XDR_ENCODE) {
    len = *sizep;
    if (len > maxsize)
      return 0;
  }

  /* Encode/decode array length */
  if (!xdr_uint(xdrs, &len))
    return 0;

  if (xdrs->op == XDR_DECODE) {
    if (len > maxsize)
      return 0;
    *sizep = len;
    if (*addrp == NULL) {
      *addrp = (char *)kalloc();  /* Allocate one page if needed */
      if (*addrp == NULL)
        return 0;
    }
  }

  /* Encode/decode each element */
  char *baseaddr = *addrp;
  for (i = 0; i < len; i++) {
    char *ep = baseaddr + i * elsize;
    if (!elproc(xdrs, ep))
      return 0;
  }
  return 1;
}

/*
 * Vector: fixed-size array (no length prefix)
 */
int
xdr_vector(XDR *xdrs, char *basep, uint nelem, uint elemsize,
           int (*xdr_elem)(XDR *, char *))
{
  uint i;

  for (i = 0; i < nelem; i++) {
    char *ep = basep + i * elemsize;
    if (!xdr_elem(xdrs, ep))
      return 0;
  }
  return 1;
}

/*
 * Discriminated union: 4-byte discriminant + selected arm
 */
int
xdr_union(XDR *xdrs, int *dscmp, char *unp, struct xdr_discrim *choices,
          int (*defaultarm)(XDR *, char *))
{
  int dscm;

  /* Encode/decode discriminant */
  if (xdrs->op == XDR_ENCODE) {
    dscm = *dscmp;
  }

  if (!xdr_int(xdrs, &dscm))
    return 0;

  if (xdrs->op == XDR_DECODE) {
    *dscmp = dscm;
  }

  /* Find and process the corresponding arm */
  for (int i = 0; choices[i].proc != NULL; i++) {
    if (choices[i].value == dscm) {
      return choices[i].proc(xdrs, unp);
    }
  }

  /* Use default arm if provided */
  if (defaultarm != NULL)
    return defaultarm(xdrs, unp);

  return 0;
}

/*
 * Calculate size of encoded data
 */
uint
xdr_sizeof(int (*func)(XDR *, void *), void *data)
{
  char buf[4096];  /* Temporary buffer */
  XDR xdrs;

  xdr_init(&xdrs, buf, sizeof(buf), XDR_ENCODE);
  if (func(&xdrs, data)) {
    return xdr_getpos(&xdrs);
  }
  return 0;
}

/*
 * Free allocated memory during decode
 * (Simplified: just clears the pointer)
 */
void
xdr_free(int (*func)(XDR *, void *), void *objp)
{
  XDR xdrs;
  char buf[1];

  xdr_init(&xdrs, buf, sizeof(buf), XDR_FREE);
  func(&xdrs, objp);
}
