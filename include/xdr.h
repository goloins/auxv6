#ifndef _XDR_H_
#define _XDR_H_

#include "types.h"

/*
 * XDR (External Data Representation) - RFC 1014
 * Machine-independent data serialization for RPC
 */

typedef enum {
  XDR_ENCODE = 0,
  XDR_DECODE = 1,
  XDR_FREE = 2
} xdr_op;

/*
 * XDR stream - encapsulates position in buffer and operation
 */
typedef struct {
  char *buf;        /* Current position in buffer */
  char *start;      /* Start of buffer */
  uint buflen;      /* Total buffer length */
  uint pos;         /* Current position */
  xdr_op op;        /* Current operation */
} XDR;

/* XDR initialization */
void xdr_init(XDR *xdrs, char *buf, uint len, xdr_op op);
void xdr_reset(XDR *xdrs);
uint xdr_getpos(XDR *xdrs);
int xdr_setpos(XDR *xdrs, uint pos);

/* Discriminated union discriminant table */
struct xdr_discrim {
  int value;
  int (*proc)(XDR *, char *);
};

/* Basic type encoding/decoding */
int xdr_int(XDR *xdrs, int *ip);
int xdr_uint(XDR *xdrs, uint *up);
int xdr_long(XDR *xdrs, long *lp);
int xdr_ulong(XDR *xdrs, ulong *ulp);
int xdr_short(XDR *xdrs, short *sp);
int xdr_ushort(XDR *xdrs, ushort *usp);
int xdr_bool(XDR *xdrs, int *bp);
int xdr_enum(XDR *xdrs, int *ep);
int xdr_float(XDR *xdrs, float *fp);
int xdr_double(XDR *xdrs, double *dp);

/* Variable-length data */
int xdr_bytes(XDR *xdrs, char **cpp, uint *sizep, uint maxsize);
int xdr_opaque(XDR *xdrs, char *cp, uint cnt);
int xdr_string(XDR *xdrs, char **cpp, uint maxsize);

/* Arrays and vectors */
int xdr_array(XDR *xdrs, char **addrp, uint *sizep, uint maxsize, uint elsize,
              int (*elproc)(XDR *, char *));
int xdr_vector(XDR *xdrs, char *basep, uint nelem, uint elemsize,
               int (*xdr_elem)(XDR *, char *));

/* Discriminated union */
int xdr_union(XDR *xdrs, int *dscmp, char *unp, struct xdr_discrim *choices,
              int (*defaultarm)(XDR *, char *));

/* Utility functions */
uint xdr_sizeof(int (*func)(XDR *, void *), void *data);
void xdr_free(int (*func)(XDR *, void *), void *objp);

#endif /* _XDR_H_ */
