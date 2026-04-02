/*
 * MOUNT Protocol Implementation (RFC 1813)
 * Service for mounting NFS filesystems
 */

#include "types.h"
#include "defs.h"
#include "mount.h"
#include "rpc.h"
#include "xdr.h"
#include "socket.h"
#include "stddef.h"

extern void *memcpy(void*, void*, uint);
extern void *memset(void*, int, uint);
extern int strlen(const char*);

/*
 * XDR codecs for MOUNT structures
 */

int
xdr_fhandle3(void *xdrs_ptr, fhandle3 *fhp)
{
  XDR *xdrs = (XDR *)xdrs_ptr;
  return xdr_opaque(xdrs, (char *)fhp->data, FHSIZE3);
}

int
xdr_mount_args(void *xdrs_ptr, mount_args *args)
{
  XDR *xdrs = (XDR *)xdrs_ptr;
  return xdr_string(xdrs, (char **)&args->dirpath, sizeof(args->dirpath));
}

int
xdr_mount_res(void *xdrs_ptr, mount_res *res)
{
  XDR *xdrs = (XDR *)xdrs_ptr;
  uint i;

  /* Decode status */
  if (!xdr_uint(xdrs, (uint *)&res->status))
    return 0;

  /* If not success, stop here */
  if (res->status != MNT_OK)
    return 1;

  /* Decode file handle */
  if (!xdr_fhandle3(xdrs, &res->fhandle))
    return 0;

  /* Decode auth flavors array length */
  if (!xdr_uint(xdrs, &res->auth_flavors_len))
    return 0;

  if (res->auth_flavors_len > 10)
    res->auth_flavors_len = 10;

  /* Decode auth flavors */
  for (i = 0; i < res->auth_flavors_len; i++) {
    if (!xdr_uint(xdrs, &res->auth_flavors[i]))
      return 0;
  }

  return 1;
}

int
xdr_umnt_args(void *xdrs_ptr, umnt_args *args)
{
  XDR *xdrs = (XDR *)xdrs_ptr;
  return xdr_string(xdrs, (char **)&args->dirpath, sizeof(args->dirpath));
}

static int
xdr_mount_args_rpc(XDR *xdrs, void *args)
{
  return xdr_mount_args((void *)xdrs, (mount_args *)args);
}

static int
xdr_mount_res_rpc(XDR *xdrs, void *res)
{
  return xdr_mount_res((void *)xdrs, (mount_res *)res);
}

static int
xdr_umnt_args_rpc(XDR *xdrs, void *args)
{
  return xdr_umnt_args((void *)xdrs, (umnt_args *)args);
}

static int
xdr_uint_rpc(XDR *xdrs, void *arg)
{
  return xdr_uint(xdrs, (uint *)arg);
}

static int
xdr_pmap_call_rpc(XDR *xdrs, void *arg)
{
  pmap_call *p = (pmap_call *)arg;

  if (!xdr_uint(xdrs, &p->prog))
    return 0;
  if (!xdr_uint(xdrs, &p->vers))
    return 0;
  if (!xdr_uint(xdrs, &p->prot))
    return 0;
  if (!xdr_uint(xdrs, &p->port))
    return 0;
  return 1;
}

/*
 * Portmapper probe to find service port
 * 
 * Contacts portmapper on server:111 to ask for the port of a service.
 * Returns port number on success, 0 on failure.
 */
ushort
pmap_getport(struct in_addr server, uint prog, uint vers, uint prot)
{
  rpc_client *pmapClient;
  pmap_call args;
  uint result_port = 0;

  /* Create RPC client for portmapper (port 111) */
  pmapClient = rpc_create(PMAPPROG, PMAPVERS, server, 111);
  if (pmapClient == NULL)
    return 0;

  args.prog = prog;
  args.vers = vers;
  args.prot = prot;
  args.port = 0;

  if (rpc_call(pmapClient, PMAPPROC_GETPORT,
               xdr_pmap_call_rpc, &args,
               xdr_uint_rpc, &result_port) < 0)
    result_port = 0;

  rpc_destroy(pmapClient);
  return (ushort)result_port;
}

/*
 * Mount an NFS export
 *
 * Contacts the MOUNT service on the server to mount the specified export.
 * Returns 0 on success, -1 on failure.
 */
int
mount_nfs(struct in_addr server, const char *export, fhandle3 *fh, uint *auth_flavor)
{
  rpc_client *client;
  mount_args args;
  mount_res res;
  uint mount_port;
  int ret = -1;

  /* Get MOUNT service port from portmapper */
  mount_port = pmap_getport(server, MOUNT_PROGRAM, MOUNT_VERSION, IPPROTO_UDP);
  if (mount_port == 0) {
    /* Default MOUNT port if portmapper fails */
    mount_port = 635;
  }

  /* Create RPC client for MOUNT service */
  client = rpc_create(MOUNT_PROGRAM, MOUNT_VERSION, server, mount_port);
  if (client == NULL)
    return -1;

  /* Prepare mount args */
  memset(&args, 0, sizeof(args));
  if (strlen(export) >= sizeof(args.dirpath) - 1)
    goto cleanup;
  memcpy(args.dirpath, (void *)export, strlen(export));
  memset(&res, 0, sizeof(res));

  if (rpc_call(client, MOUNTPROC_MNT,
               xdr_mount_args_rpc, &args,
               xdr_mount_res_rpc, &res) < 0)
    goto cleanup;

  if (res.status != MNT_OK)
    goto cleanup;

  *fh = res.fhandle;
  if (auth_flavor != NULL) {
    if (res.auth_flavors_len > 0)
      *auth_flavor = res.auth_flavors[0];
    else
      *auth_flavor = AUTH_NONE;
  }

  ret = 0;

cleanup:
  rpc_destroy(client);
  return ret;
}

/*
 * Unmount an NFS export
 */
int
umount_nfs(struct in_addr server, const char *export)
{
  rpc_client *client;
  umnt_args args;
  uint mount_port;
  int ret = -1;

  /* Get MOUNT service port */
  mount_port = pmap_getport(server, MOUNT_PROGRAM, MOUNT_VERSION, IPPROTO_UDP);
  if (mount_port == 0)
    mount_port = 635;

  /* Create RPC client */
  client = rpc_create(MOUNT_PROGRAM, MOUNT_VERSION, server, mount_port);
  if (client == NULL)
    return -1;

  /* Prepare args */
  memset(&args, 0, sizeof(args));
  if (strlen(export) >= sizeof(args.dirpath) - 1)
    goto cleanup;
  memcpy(args.dirpath, (void *)export, strlen(export));

  if (rpc_call(client, MOUNTPROC_UMNT,
               xdr_umnt_args_rpc, &args,
               0, 0) < 0)
    goto cleanup;

  ret = 0;

cleanup:
  rpc_destroy(client);
  return ret;
}
