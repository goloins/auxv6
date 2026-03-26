#include "../include/types.h"
#include "../include/user.h"
#include "../include/socket.h"

int
main(int argc, char *argv[])
{
  int fd;
  struct sockaddr_in addr;

  (void)argc;
  (void)argv;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(fd < 0){
    printf(1, "sockettest: socket() failed\n");
    exit();
  }
  printf(1, "sockettest: socket() ok fd=%d\n", fd);

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = 12345;
  addr.sin_addr = INADDR_LOOPBACK;

  if(bind(fd, &addr, sizeof(addr)) < 0){
    printf(1, "sockettest: bind() failed\n");
    close(fd);
    exit();
  }
  printf(1, "sockettest: bind() ok port=%d\n", addr.sin_port);

  if(close(fd) < 0){
    printf(1, "sockettest: close() failed\n");
    exit();
  }

  printf(1, "sockettest: PASS\n");
  exit();
}
