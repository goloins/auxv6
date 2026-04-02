#include "types.h"
#include "auxv6/user.h"
#include "socket.h"

int
main(int argc, char *argv[])
{
  int rfd, sfd;
  int pid;
  int n;
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
    printf(1, "sockettest: receiver socket() failed\n");
    exit();
  }
  printf(1, "sockettest: receiver socket fd=%d\n", rfd);

  memset(&raddr, 0, sizeof(raddr));
  raddr.sin_family = AF_INET;
  raddr.sin_port = 12345;
  raddr.sin_addr = INADDR_LOOPBACK;

  if(bind(rfd, &raddr, sizeof(raddr)) < 0){
    printf(1, "sockettest: receiver bind() failed\n");
    close(rfd);
    exit();
  }
  printf(1, "sockettest: receiver bind() ok port=%d\n", raddr.sin_port);

  if(pipe(pfd) < 0){
    printf(1, "sockettest: pipe failed\n");
    close(rfd);
    exit();
  }

  pid = fork();
  if(pid < 0){
    printf(1, "sockettest: fork failed\n");
    close(pfd[0]);
    close(pfd[1]);
    close(rfd);
    exit();
  }

  if(pid == 0){
    close(pfd[0]);
    n = recv(rfd, buf, sizeof(buf)-1);
    if(n < 0){
      n = -1;
      write(pfd[1], &n, sizeof(n));
      close(pfd[1]);
      close(rfd);
      exit();
    }
    buf[n] = '\0';
    write(pfd[1], &n, sizeof(n));
    write(pfd[1], buf, n + 1);
    close(pfd[1]);
    close(rfd);
    exit();
  }

  close(pfd[1]);

  sfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sfd < 0){
    printf(1, "sockettest: sender socket() failed\n");
    close(rfd);
    exit();
  }

  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port = 12346;
  saddr.sin_addr = INADDR_LOOPBACK;

  if(bind(sfd, &saddr, sizeof(saddr)) < 0){
    printf(1, "sockettest: sender bind() failed\n");
    close(sfd);
    close(rfd);
    exit();
  }

  if(connect(sfd, &raddr, sizeof(raddr)) < 0){
    printf(1, "sockettest: connect() failed\n");
    close(sfd);
    close(rfd);
    exit();
  }

  n = send(sfd, msg, strlen(msg));
  if(n < 0){
    printf(1, "sockettest: send() failed\n");
    close(pfd[0]);
    close(sfd);
    close(rfd);
    exit();
  }
  printf(1, "sockettest: send() ok n=%d\n", n);

  if(read(pfd[0], &child_n, sizeof(child_n)) != sizeof(child_n)) {
    printf(1, "sockettest: failed to read child recv status\n");
    close(pfd[0]);
    close(sfd);
    close(rfd);
    wait();
    exit();
  }

  if(child_n < 0){
    printf(1, "sockettest: recv() failed\n");
    close(pfd[0]);
    close(sfd);
    close(rfd);
    wait();
    exit();
  }

  if(child_n >= sizeof(child_buf))
    child_n = sizeof(child_buf) - 1;
  if(read(pfd[0], child_buf, child_n + 1) != child_n + 1) {
    printf(1, "sockettest: failed to read child payload\n");
    close(pfd[0]);
    close(sfd);
    close(rfd);
    wait();
    exit();
  }
  child_buf[child_n] = '\0';
  close(pfd[0]);

  printf(1, "sockettest: recv() ok n=%d msg=%s\n", child_n, child_buf);

  wait();

  close(sfd);
  close(rfd);

  printf(1, "sockettest: PASS send/recv over lo0\n");
  exit();
}
