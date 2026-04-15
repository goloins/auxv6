#include "types.h"
#include "auxv6/user.h"
#include "sys/socket.h"
#include "sys/un.h"
#include "socket.h"

static int
test_socketpair_rights_multi(void)
{
  int sv[2];
  int p1[2], p2[2];
  struct iovec iov;
  struct msghdr msg;
  struct cmsghdr *cmsg;
  int sendfds[2];
  int recvfds[2];
  int n;
  char payload = 'R';
  char out = 0;
  char cbuf[CMSG_SPACE(sizeof(sendfds))];
  char rcbuf[CMSG_SPACE(sizeof(recvfds))];

  if(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
    dprintf(1, "sockettest: socketpair() failed\n");
    return -1;
  }

  if(pipe(p1) < 0 || pipe(p2) < 0) {
    dprintf(1, "sockettest: pipe setup for rights test failed\n");
    close(sv[0]);
    close(sv[1]);
    return -1;
  }

  memset(&msg, 0, sizeof(msg));
  memset(cbuf, 0, sizeof(cbuf));
  iov.iov_base = &payload;
  iov.iov_len = 1;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cbuf;
  msg.msg_controllen = sizeof(cbuf);

  sendfds[0] = p1[0];
  sendfds[1] = p2[0];
  cmsg = (struct cmsghdr *)cbuf;
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(sendfds));
  memmove(CMSG_DATA(cmsg), sendfds, sizeof(sendfds));

  n = sendmsg(sv[0], &msg, 0);
  if(n != 1) {
    dprintf(1, "sockettest: sendmsg rights failed n=%d\n", n);
    close(p1[0]); close(p1[1]); close(p2[0]); close(p2[1]);
    close(sv[0]); close(sv[1]);
    return -1;
  }

  close(p1[0]);
  close(p2[0]);

  memset(&msg, 0, sizeof(msg));
  memset(rcbuf, 0, sizeof(rcbuf));
  out = 0;
  iov.iov_base = &out;
  iov.iov_len = 1;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = rcbuf;
  msg.msg_controllen = sizeof(rcbuf);

  n = recvmsg(sv[1], &msg, 0);
  if(n != 1 || out != 'R') {
    dprintf(1, "sockettest: recvmsg payload failed n=%d out=%c\n", n, out);
    close(p1[1]); close(p2[1]);
    close(sv[0]); close(sv[1]);
    return -1;
  }

  cmsg = (struct cmsghdr *)rcbuf;
  if(msg.msg_controllen < CMSG_SPACE(sizeof(recvfds)) ||
     cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
    dprintf(1, "sockettest: recvmsg missing SCM_RIGHTS\n");
    close(p1[1]); close(p2[1]);
    close(sv[0]); close(sv[1]);
    return -1;
  }

  memmove(recvfds, CMSG_DATA(cmsg), sizeof(recvfds));

  if(write(p1[1], "A", 1) != 1 || write(p2[1], "B", 1) != 1) {
    dprintf(1, "sockettest: write to source pipes failed\n");
    close(recvfds[0]); close(recvfds[1]);
    close(p1[1]); close(p2[1]);
    close(sv[0]); close(sv[1]);
    return -1;
  }

  {
    char a = 0;
    char b = 0;
    if(read(recvfds[0], &a, 1) != 1 || read(recvfds[1], &b, 1) != 1 ||
       a != 'A' || b != 'B') {
      dprintf(1, "sockettest: received fds not usable a=%c b=%c\n", a, b);
      close(recvfds[0]); close(recvfds[1]);
      close(p1[1]); close(p2[1]);
      close(sv[0]); close(sv[1]);
      return -1;
    }
  }

  close(recvfds[0]);
  close(recvfds[1]);
  close(p1[1]);
  close(p2[1]);
  close(sv[0]);
  close(sv[1]);

  dprintf(1, "sockettest: PASS socketpair sendmsg/recvmsg SCM_RIGHTS multi-fd\n");
  return 0;
}

static int
test_unix_path_stream(void)
{
  int lfd;
  int cfd;
  int afd;
  int n;
  char in[32];
  char *msg = "unix-path-ok";
  struct sockaddr_un addr;

  lfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if(lfd < 0) {
    dprintf(1, "sockettest: unix-path listener socket failed\n");
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, "/tmp/sockettest.af_unix");

  if(bind(lfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    dprintf(1, "sockettest: unix-path bind failed\n");
    close(lfd);
    return -1;
  }
  if(listen(lfd, 4) < 0) {
    dprintf(1, "sockettest: unix-path listen failed\n");
    close(lfd);
    return -1;
  }

  cfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if(cfd < 0) {
    dprintf(1, "sockettest: unix-path client socket failed\n");
    close(lfd);
    return -1;
  }
  if(connect(cfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    dprintf(1, "sockettest: unix-path connect failed\n");
    close(cfd);
    close(lfd);
    return -1;
  }

  afd = accept(lfd);
  if(afd < 0) {
    dprintf(1, "sockettest: unix-path accept failed\n");
    close(cfd);
    close(lfd);
    return -1;
  }

  n = send(cfd, msg, strlen(msg));
  if(n != (int)strlen(msg)) {
    dprintf(1, "sockettest: unix-path send failed n=%d\n", n);
    close(afd);
    close(cfd);
    close(lfd);
    return -1;
  }

  memset(in, 0, sizeof(in));
  n = recv(afd, in, sizeof(in) - 1);
  if(n != (int)strlen(msg) || strcmp(in, msg) != 0) {
    dprintf(1, "sockettest: unix-path recv mismatch n=%d msg=%s\n", n, in);
    close(afd);
    close(cfd);
    close(lfd);
    return -1;
  }

  close(afd);
  close(cfd);
  close(lfd);
  dprintf(1, "sockettest: PASS pathname AF_UNIX stream\n");
  return 0;
}

int
main(int argc, char *argv[])
{
  int rfd, sfd;
  int pid;
  int n;
  int status;
  int pfd[2];
  int child_n;
  char buf[64];
  char child_buf[64];
  char *msg = "hello over lo0";
  struct sockaddr_in raddr;
  struct sockaddr_in saddr;

  (void)argc;
  (void)argv;

  rfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(rfd < 0){
    dprintf(1, "sockettest: receiver socket() failed\n");
    exit(1);
  }
  dprintf(1, "sockettest: receiver socket fd=%d\n", rfd);

  memset(&raddr, 0, sizeof(raddr));
  raddr.sin_family = AF_INET;
  raddr.sin_port = 12345;
  raddr.sin_addr.s_addr = INADDR_LOOPBACK;

  if(bind(rfd, &raddr, sizeof(raddr)) < 0){
    dprintf(1, "sockettest: receiver bind() failed\n");
    close(rfd);
    exit(1);
  }
  dprintf(1, "sockettest: receiver bind() ok port=%d\n", raddr.sin_port);

  if(pipe(pfd) < 0){
    dprintf(1, "sockettest: pipe failed\n");
    close(rfd);
    exit(1);
  }

  pid = fork();
  if(pid < 0){
    dprintf(1, "sockettest: fork failed\n");
    close(pfd[0]);
    close(pfd[1]);
    close(rfd);
    exit(1);
  }

  if(pid == 0){
    close(pfd[0]);
    n = recv(rfd, buf, sizeof(buf)-1);
    if(n < 0){
      n = -1;
      write(pfd[1], &n, sizeof(n));
      close(pfd[1]);
      close(rfd);
      exit(1);
    }
    buf[n] = '\0';
    write(pfd[1], &n, sizeof(n));
    write(pfd[1], buf, n + 1);
    close(pfd[1]);
    close(rfd);
    exit(0);
  }

  close(pfd[1]);

  sfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sfd < 0){
    dprintf(1, "sockettest: sender socket() failed\n");
    close(rfd);
    exit(1);
  }

  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port = 12346;
  saddr.sin_addr.s_addr = INADDR_LOOPBACK;

  if(bind(sfd, &saddr, sizeof(saddr)) < 0){
    dprintf(1, "sockettest: sender bind() failed\n");
    close(sfd);
    close(rfd);
    exit(1);
  }

  if(connect(sfd, &raddr, sizeof(raddr)) < 0){
    dprintf(1, "sockettest: connect() failed\n");
    close(sfd);
    close(rfd);
    exit(1);
  }

  n = send(sfd, msg, strlen(msg));
  if(n < 0){
    dprintf(1, "sockettest: send() failed\n");
    close(pfd[0]);
    close(sfd);
    close(rfd);
    exit(1);
  }
  dprintf(1, "sockettest: send() ok n=%d\n", n);

  if(read(pfd[0], &child_n, sizeof(child_n)) != sizeof(child_n)) {
    dprintf(1, "sockettest: failed to read child recv status\n");
    close(pfd[0]);
    close(sfd);
    close(rfd);
    wait(&status);
    exit(1);
  }

  if(child_n < 0){
    dprintf(1, "sockettest: recv() failed\n");
    close(pfd[0]);
    close(sfd);
    close(rfd);
    wait(&status);
    exit(1);
  }

  if(child_n >= sizeof(child_buf))
    child_n = sizeof(child_buf) - 1;
  if(read(pfd[0], child_buf, child_n + 1) != child_n + 1) {
    dprintf(1, "sockettest: failed to read child payload\n");
    close(pfd[0]);
    close(sfd);
    close(rfd);
    wait(&status);
    exit(1);
  }
  child_buf[child_n] = '\0';
  close(pfd[0]);

  dprintf(1, "sockettest: recv() ok n=%d msg=%s\n", child_n, child_buf);

  wait(&status);

  close(sfd);
  close(rfd);

  dprintf(1, "sockettest: PASS send/recv over lo0\n");

  if(test_socketpair_rights_multi() < 0)
    exit(1);

  if(test_unix_path_stream() < 0)
    exit(1);

  exit(0);
}
