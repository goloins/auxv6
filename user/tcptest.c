#include "types.h"
#include "auxv6/user.h"
#include "socket.h"

int
main(int argc, char *argv[])
{
  int sfd, cfd, afd;
  int pid;
  int n;
  int status;
  char buf[64];
  char *msg = "hello tcp lo0";
  struct sockaddr_in saddr;

  (void)argc;
  (void)argv;

  sfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sfd < 0){
    dprintf(1, "tcptest: server socket failed\n");
    exit(1);
  }

  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port = 22345;
  saddr.sin_addr.s_addr = INADDR_LOOPBACK;

  if(bind(sfd, &saddr, sizeof(saddr)) < 0){
    dprintf(1, "tcptest: server bind failed\n");
    close(sfd);
    exit(1);
  }

  if(listen(sfd, 4) < 0){
    dprintf(1, "tcptest: server listen failed\n");
    close(sfd);
    exit(1);
  }

  pid = fork();
  if(pid < 0){
    dprintf(1, "tcptest: fork failed\n");
    close(sfd);
    exit(1);
  }

  if(pid == 0){
    cfd = socket(AF_INET, SOCK_STREAM, 0);
    if(cfd < 0){
      dprintf(1, "tcptest: client socket failed\n");
      exit(1);
    }

    if(connect(cfd, &saddr, sizeof(saddr)) < 0){
      dprintf(1, "tcptest: client connect failed\n");
      close(cfd);
      exit(1);
    }

    n = send(cfd, msg, strlen(msg));
    if(n < 0){
      dprintf(1, "tcptest: client send failed\n");
      close(cfd);
      exit(1);
    }

    close(cfd);
    exit(0);
  }

  afd = accept(sfd);
  if(afd < 0){
    dprintf(1, "tcptest: accept failed\n");
    close(sfd);
    int status;
    wait(&status);
    exit(1);
  }

  n = recv(afd, buf, sizeof(buf)-1);
  if(n < 0){
    dprintf(1, "tcptest: server recv failed\n");
    close(afd);
    close(sfd);
    wait(&status);
    exit(1);
  }
  buf[n] = '\0';

  wait(&status);
  close(afd);
  close(sfd);

  dprintf(1, "tcptest: PASS n=%d msg=%s\n", n, buf);
  exit(0);
}
