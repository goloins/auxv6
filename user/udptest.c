#include "types.h"
#include "auxv6/user.h"
#include "socket.h"

/*
 * udptest – sendto / recvfrom regression test.
 *
 * Runs in the xv6 QEMU environment against the loopback interface.
 * Each check outputs PASS or FAIL; the final line summarises the run.
 */

static int errors;
static int checks;

static void
check(const char *name, int ok)
{
  checks++;
  if(ok)
    printf(1, "  PASS: %s\n", name);
  else {
    printf(1, "  FAIL: %s\n", name);
    errors++;
  }
}

/* ------------------------------------------------------------------ *
 * Test 1 – sendto from an unbound socket (auto-bind) + recvfrom       *
 *           verifies payload and filled-in source address.            *
 * ------------------------------------------------------------------ */
static void
test_sendto_autobind(void)
{
  int sfd, cfd;
  int pid, n;
  char buf[64];
  const char *msg = "udp-autobind";
  struct sockaddr_in saddr, src;
  int srclen;

  printf(1, "[1] sendto autobind + recvfrom-with-src:\n");

  sfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sfd < 0){ check("server socket open", 0); return; }

  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port   = 33001;
  saddr.sin_addr   = INADDR_LOOPBACK;

  if(bind(sfd, &saddr, sizeof(saddr)) < 0){
    check("server bind", 0);
    close(sfd);
    return;
  }
  check("server bind", 1);

  pid = fork();
  if(pid < 0){ check("fork", 0); close(sfd); return; }

  if(pid == 0){
    /* Child – unbound sender; auto-bind happens inside sendto. */
    cfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(cfd < 0) exit();
    n = sendto(cfd, msg, strlen(msg) + 1, 0, &saddr, sizeof(saddr));
    close(cfd);
    if(n < 0) exit();
    exit();
  }

  /* Parent – server-side recvfrom. */
  memset(&src, 0, sizeof(src));
  srclen = sizeof(src);
  n = recvfrom(sfd, buf, sizeof(buf) - 1, 0, &src, &srclen);
  buf[sizeof(buf) - 1] = 0;

  check("recvfrom returned data",   n > 0);
  check("payload correct",          n > 0 && strcmp(buf, msg) == 0);
  check("src port auto-assigned",   src.sin_port != 0);
  check("src addr is loopback",     src.sin_addr == INADDR_LOOPBACK);
  check("srclen filled",            srclen == (int)sizeof(struct sockaddr_in));

  wait();
  close(sfd);
}

/* ------------------------------------------------------------------ *
 * Test 2 – round-trip with explicit bind on both sockets;             *
 *           server recvfrom supplies src addr, then replies;          *
 *           client recvfrom is called with NULL src / NULL srclen.    *
 * ------------------------------------------------------------------ */
static void
test_roundtrip_null_src(void)
{
  int sfd, cfd;
  int pid, n;
  char buf[64];
  const char *req  = "ping";
  const char *resp = "pong";
  struct sockaddr_in saddr, caddr, src;
  int srclen;

  printf(1, "[2] round-trip + recvfrom(NULL src):\n");

  sfd = socket(AF_INET, SOCK_DGRAM, 0);
  cfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sfd < 0 || cfd < 0){ check("socket pair", 0); return; }

  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port   = 33002;
  saddr.sin_addr   = INADDR_LOOPBACK;

  memset(&caddr, 0, sizeof(caddr));
  caddr.sin_family = AF_INET;
  caddr.sin_port   = 33003;
  caddr.sin_addr   = INADDR_LOOPBACK;

  if(bind(sfd, &saddr, sizeof(saddr)) < 0){ check("server bind", 0); goto cleanup; }
  if(bind(cfd, &caddr, sizeof(caddr)) < 0){ check("client bind", 0); goto cleanup; }
  check("both sockets bound", 1);

  pid = fork();
  if(pid < 0){ check("fork", 0); goto cleanup; }

  if(pid == 0){
    /* Child – client: send request, receive reply with NULL src pointer. */
    close(sfd);
    n = sendto(cfd, req, strlen(req) + 1, 0, &saddr, sizeof(saddr));
    if(n < 0){ close(cfd); exit(); }
    /* recvfrom with NULL src and NULL srclen – must not fault. */
    n = recvfrom(cfd, buf, sizeof(buf) - 1, 0, 0, 0);
    close(cfd);
    exit();
  }

  /* Parent – server: receive request, verify, then reply. */
  close(cfd);
  memset(&src, 0, sizeof(src));
  srclen = sizeof(src);
  n = recvfrom(sfd, buf, sizeof(buf) - 1, 0, &src, &srclen);
  buf[sizeof(buf) - 1] = 0;

  check("server recvfrom ok",     n > 0);
  check("request payload",        n > 0 && strcmp(buf, req) == 0);
  check("src port is client port", src.sin_port == caddr.sin_port);

  /* Reply back to client. */
  n = sendto(sfd, resp, strlen(resp) + 1, 0, &src, sizeof(src));
  check("server reply ok",        n > 0);

  wait();
  close(sfd);
  return;

cleanup:
  close(sfd);
  close(cfd);
}

/* ------------------------------------------------------------------ *
 * Test 3 – sendto with NULL dst falls back to connected remote_addr   *
 * ------------------------------------------------------------------ */
static void
test_sendto_connected(void)
{
  int sfd, cfd;
  int pid, n;
  char buf[32];
  const char *msg = "connected-dst";
  struct sockaddr_in saddr, caddr, src;
  int srclen;

  printf(1, "[3] sendto(NULL dst) via connected socket:\n");

  sfd = socket(AF_INET, SOCK_DGRAM, 0);
  cfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sfd < 0 || cfd < 0){ check("socket pair", 0); return; }

  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port   = 33004;
  saddr.sin_addr   = INADDR_LOOPBACK;

  memset(&caddr, 0, sizeof(caddr));
  caddr.sin_family = AF_INET;
  caddr.sin_port   = 33005;
  caddr.sin_addr   = INADDR_LOOPBACK;

  if(bind(sfd, &saddr, sizeof(saddr)) < 0){ check("server bind", 0); goto cleanup; }
  if(bind(cfd, &caddr, sizeof(caddr)) < 0){ check("client bind", 0); goto cleanup; }
  /* connect client so remote_addr is populated for the NULL-dst path */
  if(connect(cfd, &saddr, sizeof(saddr)) < 0){ check("client connect", 0); goto cleanup; }
  check("prepare", 1);

  pid = fork();
  if(pid < 0){ check("fork", 0); goto cleanup; }

  if(pid == 0){
    /* Child – pass NULL for dst, relying on remote_addr set by connect. */
    close(sfd);
    n = sendto(cfd, msg, strlen(msg) + 1, 0, 0, 0);
    close(cfd);
    if(n < 0) exit();
    exit();
  }

  /* Parent – server side. */
  close(cfd);
  memset(&src, 0, sizeof(src));
  srclen = sizeof(src);
  n = recvfrom(sfd, buf, sizeof(buf) - 1, 0, &src, &srclen);
  buf[sizeof(buf) - 1] = 0;

  check("recvfrom data",    n > 0);
  check("payload correct",  n > 0 && strcmp(buf, msg) == 0);
  check("src port correct", src.sin_port == caddr.sin_port);

  wait();
  close(sfd);
  return;

cleanup:
  close(sfd);
  close(cfd);
}

int
main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  printf(1, "udptest: starting\n");

  test_sendto_autobind();
  test_roundtrip_null_src();
  test_sendto_connected();

  if(errors == 0)
    printf(1, "udptest: PASS %d checks\n", checks);
  else
    printf(1, "udptest: FAIL %d/%d checks failed\n", errors, checks);

  exit();
}
