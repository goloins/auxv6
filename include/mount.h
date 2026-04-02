#ifndef _MOUNT_H_
#define _MOUNT_H_

#include "types.h"

/*
 * MOUNT Protocol (RFC 1813 - NFS v3 MOUNT)
 * Service for mounting NFS filesystems
 */

#define MOUNT_PROGRAM    100005
#define MOUNT_VERSION    3

/* MOUNT procedures */
typedef enum {
  MOUNTPROC_NULL = 0,
  MOUNTPROC_MNT = 1,
  MOUNTPROC_DUMP = 2,
  MOUNTPROC_UMNT = 3,
  MOUNTPROC_UMNTALL = 4,
  MOUNTPROC_EXPORT = 5
} mount_proc;

/* File handle sizes */
#define FHSIZE        64
#define FHSIZE3       64

/* Mount status codes */
typedef enum {
  MNT_OK = 0,
  ENAMETOOLONG = 63,
  ENOENT = 2,
  EACCES = 13,
  EINVAL = 22,
  ENOTDIR = 20
} mount_stat;

/*
 * File handle - fixed 64 bytes for NFSv3
 */
typedef struct {
  uchar data[FHSIZE3];
} fhandle3;

/*
 * Mount result
 */
typedef struct {
  mount_stat status;
  fhandle3 fhandle;
  uint auth_flavors_len;
  uint auth_flavors[10];  /* Common flavors: AUTH_NONE, AUTH_UNIX, AUTH_DES */
} mount_res;

/*
 * Mount arguments
 */
typedef struct {
  char dirpath[1024];     /* Path to export on server */
} mount_args;

/*
 * Unmount arguments
 */
typedef struct {
  char dirpath[1024];
} umnt_args;

/*
 * Portmapper structures (for service discovery)
 */
#define PMAPPROC_GETPORT 3
#define PMAPPROG         100000
#define PMAPVERS         2

typedef struct {
  uint prog;
  uint vers;
  uint prot;              /* IPPROTO_UDP=17, IPPROTO_TCP=6 */
  uint port;
} pmap;

typedef struct {
  uint prog;
  uint vers;
  uint prot;
  uint port;
} pmap_call;

/*
 * XDR codecs for MOUNT structures
 */
int xdr_mount_args(void *xdrs, mount_args *args);
int xdr_mount_res(void *xdrs, mount_res *res);
int xdr_umnt_args(void *xdrs, umnt_args *args);
int xdr_fhandle3(void *xdrs, fhandle3 *fhp);

/*
 * MOUNT service client functions
 */
struct in_addr;
ushort pmap_getport(struct in_addr server, uint prog, uint vers, uint prot);

#endif /* _MOUNT_H_ */
