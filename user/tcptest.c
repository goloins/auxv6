#include "../include/types.h"
#include "../include/user.h"
#include "../include/socket.h"

int
main(int argc, char *argv[])
{
  int sfd, cfd;
  int pid;
  int n;
  char buf[64];
  char *msg = "hello tcp lo0";
  struct sockaddr_in saddr;

  (void)argc;
  (void)argv;

  sfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sfd < 0){
    printf(1, "tcptest: server socket failed\n");
    exit();
  }

  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port = 22345;
  saddr.sin_addr = INADDR_LOOPBACK;

  if(bind(sfd, &saddr, sizeof(saddr)) < 0){
    printf(1, "tcptest: server bind failed\n");
    close(sfd);
    exit();
  }

  pid = fork();
  if(pid < 0){
    printf(1, "tcptest: fork failed\n");
    close(sfd);
    exit();
  }

  if(pid == 0){
    cfd = socket(AF_INET, SOCK_STREAM, 0);
    if(cfd < 0){
      printf(1, "tcptest: client socket failed\n");
      exit();
    }

    if(connect(cfd, &saddr, sizeof(saddr)) < 0){
      printf(1, "tcptest: client connect failed\n");
      close(cfd);
      exit();
    }

    n = send(cfd, msg, strlen(msg));
    if(n < 0){
      printf(1, "tcptest: client send failed\n");
      close(cfd);
      exit();
    }

    close(cfd);
    exit();
  }

  n = recv(sfd, buf, sizeof(buf)-1);
  if(n < 0){
    printf(1, "tcptest: server recv failed\n");
    close(sfd);
    wait();
    exit();
  }
  buf[n] = '\0';

  wait();
  close(sfd);

  printf(1, "tcptest: PASS n=%d msg=%s\n", n, buf);
  exit();
}
