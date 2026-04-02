/*
 * NFS v3 Protocol Implementation (RFC 1813)
 * Remote file access and attribute retrieval
 */

#include "types.h"
#include "defs.h"
#include "nfs.h"
#include "rpc.h"
#include "xdr.h"
#include "socket.h"
#include "stddef.h"

extern void *memcpy(void*, void*, uint);
extern void *memset(void*, int, uint);
extern int strlen(const char*);

/*
 * XDR codec for file attributes
 */
int
xdr_fattr3(void *xdrs_ptr, fattr3 *attr)
{
  XDR *xdrs = (XDR *)xdrs_ptr;

  if (!xdr_uint(xdrs, (uint *)&attr->type))
    return 0;
  if (!xdr_uint(xdrs, &attr->mode))
    return 0;
  if (!xdr_uint(xdrs, &attr->nlink))
    return 0;
  if (!xdr_uint(xdrs, &attr->uid))
    return 0;
  if (!xdr_uint(xdrs, &attr->gid))
    return 0;
  if (!xdr_ulong(xdrs, &attr->size))
    return 0;
  if (!xdr_ulong(xdrs, &attr->used))
    return 0;
  if (!xdr_uint(xdrs, &attr->rdev_major))
    return 0;
  if (!xdr_uint(xdrs, &attr->rdev_minor))
    return 0;
  if (!xdr_ulong(xdrs, &attr->fsid))
    return 0;
  if (!xdr_ulong(xdrs, &attr->fileid))
    return 0;
  /* atime */
  if (!xdr_uint(xdrs, &attr->atime_sec))
    return 0;
  if (!xdr_uint(xdrs, &attr->atime_nsec))
    return 0;
  /* mtime */
  if (!xdr_uint(xdrs, &attr->mtime_sec))
    return 0;
  if (!xdr_uint(xdrs, &attr->mtime_nsec))
    return 0;
  /* ctime */
  if (!xdr_uint(xdrs, &attr->ctime_sec))
    return 0;
  if (!xdr_uint(xdrs, &attr->ctime_nsec))
    return 0;

  return 1;
}

/*
 * XDR codec for post-operation attributes (optional)
 */
int
xdr_post_op_attr(void *xdrs_ptr, post_op_attr *poa)
{
  XDR *xdrs = (XDR *)xdrs_ptr;

  if (!xdr_bool(xdrs, &poa->attributes_follow))
    return 0;

  if (poa->attributes_follow) {
    if (!xdr_fattr3(xdrs, &poa->attr))
      return 0;
  }

  return 1;
}

/*
 * GETATTR argument codec
 */
int
xdr_getattr3args(void *xdrs_ptr, getattr3args *args)
{
  XDR *xdrs = (XDR *)xdrs_ptr;
  return xdr_fhandle3(xdrs, &args->object);
}

/*
 * GETATTR result codec
 */
int
xdr_getattr3res(void *xdrs_ptr, getattr3res *res)
{
  XDR *xdrs = (XDR *)xdrs_ptr;

  if (!xdr_uint(xdrs, (uint *)&res->status))
    return 0;

  if (res->status == NFS3_OK) {
    if (!xdr_fattr3(xdrs, &res->obj_attributes))
      return 0;
  }

  return 1;
}

/*
 * LOOKUP argument codec
 */
int
xdr_lookup3args(void *xdrs_ptr, lookup3args *args)
{
  XDR *xdrs = (XDR *)xdrs_ptr;

  if (!xdr_fhandle3(xdrs, &args->dir))
    return 0;
  if (!xdr_string(xdrs, (char **)&args->name, sizeof(args->name)))
    return 0;

  return 1;
}

/*
 * LOOKUP result codec
 */
int
xdr_lookup3res(void *xdrs_ptr, lookup3res *res)
{
  XDR *xdrs = (XDR *)xdrs_ptr;

  if (!xdr_uint(xdrs, (uint *)&res->status))
    return 0;

  if (res->status == NFS3_OK) {
    if (!xdr_fhandle3(xdrs, &res->object))
      return 0;
  }

  if (!xdr_post_op_attr(xdrs, &res->obj_attributes))
    return 0;
  if (!xdr_post_op_attr(xdrs, &res->dir_attributes))
    return 0;

  return 1;
}

/*
 * READ argument codec
 */
int
xdr_read3args(void *xdrs_ptr, read3args *args)
{
  XDR *xdrs = (XDR *)xdrs_ptr;

  if (!xdr_fhandle3(xdrs, &args->file))
    return 0;
  if (!xdr_ulong(xdrs, &args->offset))
    return 0;
  if (!xdr_uint(xdrs, &args->count))
    return 0;

  return 1;
}

/*
 * READ result codec
 */
int
xdr_read3res(void *xdrs_ptr, read3res *res)
{
  XDR *xdrs = (XDR *)xdrs_ptr;

  if (!xdr_uint(xdrs, (uint *)&res->status))
    return 0;

  if (!xdr_post_op_attr(xdrs, &res->file_attributes))
    return 0;

  if (res->status == NFS3_OK) {
    if (!xdr_uint(xdrs, &res->count))
      return 0;
    if (!xdr_bool(xdrs, &res->eof))
      return 0;
    if (!xdr_bytes(xdrs, &res->data, &res->data_len, 65536))
      return 0;
  }

  return 1;
}

/*
 * READDIR argument codec
 */
int
xdr_readdir3args(void *xdrs_ptr, readdir3args *args)
{
  XDR *xdrs = (XDR *)xdrs_ptr;

  if (!xdr_fhandle3(xdrs, &args->dir))
    return 0;
  if (!xdr_ulong(xdrs, &args->cookie))
    return 0;
  if (!xdr_opaque(xdrs, args->cookieverf, 8))
    return 0;
  if (!xdr_uint(xdrs, &args->count))
    return 0;

  return 1;
}

/*
 * READDIR result codec (simplified - single entry for now)
 */
int
xdr_readdir3res(void *xdrs_ptr, readdir3res *res)
{
  XDR *xdrs = (XDR *)xdrs_ptr;

  if (!xdr_uint(xdrs, (uint *)&res->status))
    return 0;

  if (!xdr_post_op_attr(xdrs, &res->dir_attributes))
    return 0;

  if (res->status == NFS3_OK) {
    if (!xdr_opaque(xdrs, res->cookieverf, 8))
      return 0;
    /* TODO: Decode entry list */
    res->entries_count = 0;
  }

  return 1;
}

static int
xdr_getattr3args_rpc(XDR *xdrs, void *args)
{
  return xdr_getattr3args((void *)xdrs, (getattr3args *)args);
}

static int
xdr_getattr3res_rpc(XDR *xdrs, void *res)
{
  return xdr_getattr3res((void *)xdrs, (getattr3res *)res);
}

static int
xdr_lookup3args_rpc(XDR *xdrs, void *args)
{
  return xdr_lookup3args((void *)xdrs, (lookup3args *)args);
}

static int
xdr_lookup3res_rpc(XDR *xdrs, void *res)
{
  return xdr_lookup3res((void *)xdrs, (lookup3res *)res);
}

static int
xdr_read3args_rpc(XDR *xdrs, void *args)
{
  return xdr_read3args((void *)xdrs, (read3args *)args);
}

static int
xdr_read3res_rpc(XDR *xdrs, void *res)
{
  return xdr_read3res((void *)xdrs, (read3res *)res);
}

static int
xdr_readdir3args_rpc(XDR *xdrs, void *args)
{
  return xdr_readdir3args((void *)xdrs, (readdir3args *)args);
}

static int
xdr_readdir3res_rpc(XDR *xdrs, void *res)
{
  return xdr_readdir3res((void *)xdrs, (readdir3res *)res);
}

/*
 * Get file attributes via NFS GETATTR
 */
int
nfs_getattr(nfs_mount *mnt, fhandle3 *fh, fattr3 *attr)
{
  rpc_client *client;
  getattr3args args;
  getattr3res res;

  if (mnt == NULL || fh == NULL || attr == NULL)
    return -1;

  /* Create RPC client for NFS service */
  client = rpc_create(NFS_PROGRAM, NFS_VERSION, mnt->server, mnt->nfs_port);
  if (client == NULL)
    return -1;
  memset(&res, 0, sizeof(res));

  /* Prepare arguments */
  args.object = *fh;

  if (rpc_call(client, NFSPROC_GETATTR,
               xdr_getattr3args_rpc, &args,
               xdr_getattr3res_rpc, &res) < 0)
    goto error;
  if (res.status != NFS3_OK)
    goto error;

  *attr = res.obj_attributes;
  rpc_destroy(client);
  return 0;

error:
  rpc_destroy(client);
  return -1;
}

/*
 * Lookup a name in a directory
 */
int
nfs_lookup(nfs_mount *mnt, fhandle3 *dir, const char *name, fhandle3 *obj)
{
  rpc_client *client;
  lookup3args args;
  lookup3res res;

  if (mnt == NULL || dir == NULL || name == NULL || obj == NULL)
    return -1;

  /* Create RPC client */
  client = rpc_create(NFS_PROGRAM, NFS_VERSION, mnt->server, mnt->nfs_port);
  if (client == NULL)
    return -1;

  /* Initialize response structure */
  memset(&res, 0, sizeof(res));

  /* Prepare arguments */
  args.dir = *dir;
  if (strlen(name) >= sizeof(args.name) - 1)
    goto error;
  memcpy(args.name, (void *)name, strlen(name));
  args.name[strlen(name)] = '\0';

  if (rpc_call(client, NFSPROC_LOOKUP,
               xdr_lookup3args_rpc, &args,
               xdr_lookup3res_rpc, &res) < 0)
    goto error;
  if (res.status != NFS3_OK)
    goto error;

  *obj = res.object;
  rpc_destroy(client);
  return 0;

error:
  rpc_destroy(client);
  return -1;
}

/*
 * Read file data
 */
int
nfs_read(nfs_mount *mnt, fhandle3 *fh, ulong offset, uint count, char *buf, uint *nread)
{
  rpc_client *client;
  read3args args;
  read3res res;

  if (mnt == NULL || fh == NULL || buf == NULL || nread == NULL)
    return -1;

  /* Create RPC client */
  client = rpc_create(NFS_PROGRAM, NFS_VERSION, mnt->server, mnt->nfs_port);
  if (client == NULL)
    return -1;

  /* Initialize response structure */
  memset(&res, 0, sizeof(res));

  /* Prepare arguments */
  args.file = *fh;
  args.offset = offset;
  args.count = count;

  if (rpc_call(client, NFSPROC_READ,
               xdr_read3args_rpc, &args,
               xdr_read3res_rpc, &res) < 0)
    goto error;
  if (res.status != NFS3_OK)
    goto error;

  if (res.count > count)
    res.count = count;
  if (res.data && res.count > 0)
    memcpy(buf, res.data, res.count);

  *nread = res.count;
  rpc_destroy(client);
  return 0;

error:
  rpc_destroy(client);
  return -1;
}

/*
 * Read directory entries
 */
int
nfs_readdir(nfs_mount *mnt, fhandle3 *dir, ulong cookie, char *cookieverf, entry3 *entries, uint *count)
{
  rpc_client *client;
  readdir3args args;
  readdir3res res;

  if (mnt == NULL || dir == NULL || entries == NULL || count == NULL)
    return -1;

  /* Create RPC client */
  client = rpc_create(NFS_PROGRAM, NFS_VERSION, mnt->server, mnt->nfs_port);
  if (client == NULL)
    return -1;

  /* Initialize response structure */
  memset(&res, 0, sizeof(res));

  /* Prepare arguments */
  args.dir = *dir;
  args.cookie = cookie;
  if (cookieverf)
    memcpy(args.cookieverf, cookieverf, 8);
  else
    memset(args.cookieverf, 0, 8);
  args.count = 4096;  /* Request 4KB of directory entries */

  if (rpc_call(client, NFSPROC_READDIR,
               xdr_readdir3args_rpc, &args,
               xdr_readdir3res_rpc, &res) < 0)
    goto error;
  if (res.status != NFS3_OK)
    goto error;

  (void)entries;

  *count = res.entries_count;
  rpc_destroy(client);
  return 0;

error:
  rpc_destroy(client);
  return -1;
}
