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

/*
 * Portmapper probe to find service port
 * 
 * Contacts portmapper on server:111 to ask for the port of a service.
 * Returns port number on success, 0 on failure.
 */
ushort
pmap_getport(struct in_addr server, uint prog, uint vers, uint prot)
{
  XDR xdrs;
  char callbuf[256];
  rpc_client *pmapClient;
  rpc_msg_header msg_hdr;
  rpc_call_header call_hdr;
  uint result_port = 0;

  /* Create RPC client for portmapper (port 111) */
  pmapClient = rpc_create(PMAPPROG, PMAPVERS, server, 111);
  if (pmapClient == NULL)
    return 0;

  /* Build GETPORT call */
  msg_hdr.xid = pmapClient->xid++;
  msg_hdr.mtype = CALL;

  memset(&call_hdr, 0, sizeof(call_hdr));
  call_hdr.rpcvers = RPC_MSG_VERSION;
  call_hdr.prog = PMAPPROG;
  call_hdr.vers = PMAPVERS;
  call_hdr.proc = PMAPPROC_GETPORT;
  call_hdr.cred.flavor = AUTH_NONE;
  call_hdr.cred.len = 0;
  call_hdr.verf.flavor = AUTH_NONE;
  call_hdr.verf.len = 0;

  /* Encode GETPORT call */
  xdr_init(&xdrs, callbuf, sizeof(callbuf), XDR_ENCODE);

  if (!xdr_rpc_msg_header(&xdrs, &msg_hdr))
    goto cleanup;

  if (!xdr_rpc_call_header(&xdrs, &call_hdr))
    goto cleanup;

  /* Encode GETPORT arguments (4 uint32s: prog, vers, prot, port) */
  if (!xdr_uint(&xdrs, &prog))
    goto cleanup;
  if (!xdr_uint(&xdrs, &vers))
    goto cleanup;
  if (!xdr_uint(&xdrs, &prot))
    goto cleanup;

  /* TODO: Send callbuf via UDP to server:111 */
  /* TODO: Receive reply and decode result_port */
  /* For now, stub returns 0 */

  result_port = 0;

cleanup:
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
  XDR xdrs;
  char callbuf[512];
  rpc_msg_header msg_hdr;
  rpc_call_header call_hdr;
  mount_args args;
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

  /* Build MOUNT call */
  msg_hdr.xid = client->xid++;
  msg_hdr.mtype = CALL;

  memset(&call_hdr, 0, sizeof(call_hdr));
  call_hdr.rpcvers = RPC_MSG_VERSION;
  call_hdr.prog = MOUNT_PROGRAM;
  call_hdr.vers = MOUNT_VERSION;
  call_hdr.proc = MOUNTPROC_MNT;
  call_hdr.cred.flavor = AUTH_NONE;
  call_hdr.cred.len = 0;
  call_hdr.verf.flavor = AUTH_NONE;
  call_hdr.verf.len = 0;

  /* Prepare mount args */
  memset(&args, 0, sizeof(args));
  if (strlen(export) >= sizeof(args.dirpath) - 1)
    goto cleanup;
  memcpy(args.dirpath, (void *)export, strlen(export));

  /* Encode MOUNT call */
  xdr_init(&xdrs, callbuf, sizeof(callbuf), XDR_ENCODE);

  if (!xdr_rpc_msg_header(&xdrs, &msg_hdr))
    goto cleanup;

  if (!xdr_rpc_call_header(&xdrs, &call_hdr))
    goto cleanup;

  if (!xdr_mount_args(&xdrs, &args))
    goto cleanup;

  /* TODO: Send callbuf via UDP to server:mount_port */
  /* TODO: Receive reply and decode mount_res */
  /* TODO: Extract fhandle and auth_flavor from response */

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
  XDR xdrs;
  char callbuf[512];
  rpc_msg_header msg_hdr;
  rpc_call_header call_hdr;
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

  /* Build UNMOUNT call */
  msg_hdr.xid = client->xid++;
  msg_hdr.mtype = CALL;

  memset(&call_hdr, 0, sizeof(call_hdr));
  call_hdr.rpcvers = RPC_MSG_VERSION;
  call_hdr.prog = MOUNT_PROGRAM;
  call_hdr.vers = MOUNT_VERSION;
  call_hdr.proc = MOUNTPROC_UMNT;
  call_hdr.cred.flavor = AUTH_NONE;
  call_hdr.cred.len = 0;
  call_hdr.verf.flavor = AUTH_NONE;
  call_hdr.verf.len = 0;

  /* Prepare args */
  memset(&args, 0, sizeof(args));
  if (strlen(export) >= sizeof(args.dirpath) - 1)
    goto cleanup;
  memcpy(args.dirpath, (void *)export, strlen(export));

  /* Encode UNMOUNT call */
  xdr_init(&xdrs, callbuf, sizeof(callbuf), XDR_ENCODE);

  if (!xdr_rpc_msg_header(&xdrs, &msg_hdr))
    goto cleanup;

  if (!xdr_rpc_call_header(&xdrs, &call_hdr))
    goto cleanup;

  if (!xdr_umnt_args(&xdrs, &args))
    goto cleanup;

  /* TODO: Send callbuf via UDP */
  /* UMNT doesn't require a response, just send and return */

  ret = 0;

cleanup:
  rpc_destroy(client);
  return ret;
}
