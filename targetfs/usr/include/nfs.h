#ifndef _NFS_H_
#define _NFS_H_

#include "types.h"
#include "socket.h"
#include "mount.h"

/*
 * Network File System (NFS) v3 - RFC 1813
 * Remote file access and attribute retrieval
 */

#define NFS_PROGRAM      100003
#define NFS_VERSION      3

/* NFS procedures */
typedef enum {
  NFSPROC_NULL = 0,
  NFSPROC_GETATTR = 1,
  NFSPROC_SETATTR = 2,
  NFSPROC_LOOKUP = 3,
  NFSPROC_ACCESS = 4,
  NFSPROC_READLINK = 5,
  NFSPROC_READ = 6,
  NFSPROC_WRITE = 7,
  NFSPROC_CREATE = 8,
  NFSPROC_MKDIR = 9,
  NFSPROC_SYMLINK = 10,
  NFSPROC_MKNOD = 11,
  NFSPROC_REMOVE = 12,
  NFSPROC_RMDIR = 13,
  NFSPROC_RENAME = 14,
  NFSPROC_LINK = 15,
  NFSPROC_READDIR = 16,
  NFSPROC_READDIRPLUS = 17,
  NFSPROC_FSSTAT = 18,
  NFSPROC_FSINFO = 19,
  NFSPROC_PATHCONF = 20,
  NFSPROC_COMMIT = 21
} nfs_proc;

/* NFS status codes */
typedef enum {
  NFS3_OK = 0,
  NFS3ERR_PERM = 1,
  NFS3ERR_NOENT = 2,
  NFS3ERR_IO = 5,
  NFS3ERR_NXIO = 6,
  NFS3ERR_ACCES = 13,
  NFS3ERR_EXIST = 17,
  NFS3ERR_XDEV = 18,
  NFS3ERR_NODEV = 19,
  NFS3ERR_NOTDIR = 20,
  NFS3ERR_ISDIR = 21,
  NFS3ERR_INVAL = 22,
  NFS3ERR_FBIG = 27,
  NFS3ERR_NOSPC = 28,
  NFS3ERR_ROFS = 30,
  NFS3ERR_MLINK = 31,
  NFS3ERR_NAMETOOLONG = 63,
  NFS3ERR_NOTEMPTY = 66,
  NFS3ERR_DQUOT = 69,
  NFS3ERR_STALE = 70,
  NFS3ERR_REMOTE = 71,
  NFS3ERR_BADHANDLE = 10001,
  NFS3ERR_NOT_SYNC = 10002,
  NFS3ERR_BAD_COOKIE = 10003,
  NFS3ERR_NOTSUPP = 10004,
  NFS3ERR_TOOSMALL = 10005,
  NFS3ERR_SERVERFAULT = 10006,
  NFS3ERR_BADTYPE = 10007,
  NFS3ERR_JUKEBOX = 10008
} nfs_stat;

/* File types */
typedef enum {
  NF3REG = 1,      /* Regular file */
  NF3DIR = 2,      /* Directory */
  NF3BLK = 3,      /* Block device */
  NF3CHR = 4,      /* Character device */
  NF3LNK = 5,      /* Symbolic link */
  NF3SOCK = 6,     /* Socket */
  NF3FIFO = 7      /* Named pipe */
} ftype3;

/* File attributes */
typedef struct {
  ftype3 type;
  uint mode;
  uint nlink;
  uint uid;
  uint gid;
  ulong size;
  ulong used;
  uint rdev_major;
  uint rdev_minor;
  ulong fsid;
  ulong fileid;
  uint atime_sec;
  uint atime_nsec;
  uint mtime_sec;
  uint mtime_nsec;
  uint ctime_sec;
  uint ctime_nsec;
} fattr3;

/* Post-operation attributes */
typedef struct {
  int attributes_follow;  /* 0 or 1 */
  fattr3 attr;
} post_op_attr;

/* Pre-operation attributes */
typedef struct {
  int attributes_follow;  /* 0 or 1 */
  uint size;
  uint mtime_sec;
  uint mtime_nsec;
} pre_op_attr;

/* GETATTR arguments and result */
typedef struct {
  fhandle3 object;
} getattr3args;

typedef struct {
  nfs_stat status;
  fattr3 obj_attributes;
} getattr3res;

/* LOOKUP arguments and result */
typedef struct {
  fhandle3 dir;
  char name[256];         /* Filename to lookup */
} lookup3args;

typedef struct {
  nfs_stat status;
  fhandle3 object;        /* File handle of found object */
  post_op_attr obj_attributes;
  post_op_attr dir_attributes;
} lookup3res;

/* READ arguments and result */
typedef struct {
  fhandle3 file;
  ulong offset;
  uint count;
} read3args;

typedef struct {
  nfs_stat status;
  post_op_attr file_attributes;
  uint count;
  int eof;
  uint data_len;
  char *data;
} read3res;

/* READDIR arguments and result */
typedef struct {
  fhandle3 dir;
  ulong cookie;           /* Cookie from previous READDIR */
  char cookieverf[8];     /* Cookie verifier */
  uint count;             /* Bytes of directory entries desired */
} readdir3args;

typedef struct {
  uint namelen;
  char name[256];
  ulong cookie;
  post_op_attr attr;
} entry3;

typedef struct {
  nfs_stat status;
  post_op_attr dir_attributes;
  char cookieverf[8];
  uint entries_count;
  entry3 *entries;        /* Dynamic array */
  int eof;
} readdir3res;

/* FSSTAT arguments and result */
typedef struct {
  fhandle3 fsroot;
} fsstat3args;

typedef struct {
  nfs_stat status;
  post_op_attr obj_attributes;
  ulong tbytes;           /* Total size */
  ulong fbytes;           /* Free size */
  ulong abytes;           /* Available size */
  ulong tfiles;           /* Total files */
  ulong ffiles;           /* Free files */
  ulong afiles;           /* Available files */
  uint invarsec;          /* Seconds in invariant */
} fsstat3res;

/* NFS mount information - kept in kernel */
typedef struct {
  struct in_addr server;
  char export[256];
  fhandle3 rootfh;        /* Root file handle for this export */
  uint nfs_port;          /* NFS service port from portmapper */
  uint timeout_ms;        /* RPC timeout */
} nfs_mount;

/*
 * XDR codecs for NFS structures
 */
int xdr_fattr3(void *xdrs, fattr3 *attr);
int xdr_post_op_attr(void *xdrs, post_op_attr *poa);
int xdr_getattr3args(void *xdrs, getattr3args *args);
int xdr_getattr3res(void *xdrs, getattr3res *res);
int xdr_lookup3args(void *xdrs, lookup3args *args);
int xdr_lookup3res(void *xdrs, lookup3res *res);
int xdr_read3args(void *xdrs, read3args *args);
int xdr_read3res(void *xdrs, read3res *res);
int xdr_readdir3args(void *xdrs, readdir3args *args);
int xdr_readdir3res(void *xdrs, readdir3res *res);

/*
 * NFS client functions (in kernel)
 */
int nfs_getattr(nfs_mount *mnt, fhandle3 *fh, fattr3 *attr);
int nfs_lookup(nfs_mount *mnt, fhandle3 *dir, const char *name, fhandle3 *obj);
int nfs_read(nfs_mount *mnt, fhandle3 *fh, ulong offset, uint count, char *buf, uint *nread);
int nfs_readdir(nfs_mount *mnt, fhandle3 *dir, ulong cookie, char *cookieverf, entry3 *entries, uint *count);

#endif /* _NFS_H_ */
