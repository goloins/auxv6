/*
 * testdaemon.c - Simple test of double-fork daemonization
 *
 * Tests whether the double-fork + setsid + stdio redirect pattern
 * works correctly, to isolate whether devman -d's bootloop is due to
 * the daemonization strategy or to devman-specific logic.
 */

#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"

int
main(void)
{
  int pid;
  int i;

  dprintf(1, "testdaemon: starting\n");

  /* First fork */
  pid = fork();
  if(pid < 0){
    dprintf(2, "testdaemon: first fork failed\n");
    exit(1);
  }
  if(pid > 0){
    dprintf(1, "testdaemon: parent exiting (pid %d -> child %d)\n", getpid(), pid);
    exit(0);
  }

  dprintf(1, "testdaemon: first child (pid %d)\n", getpid());

  /* Create new session */
  if(setsid() < 0){
    dprintf(2, "testdaemon: setsid failed\n");
    exit(1);
  }
  dprintf(1, "testdaemon: setsid ok, new sid\n");

  /* Second fork */
  pid = fork();
  if(pid < 0){
    dprintf(2, "testdaemon: second fork failed\n");
    exit(1);
  }
  if(pid > 0){
    dprintf(1, "testdaemon: intermediate child exiting (pid %d -> grandchild %d)\n", getpid(), pid);
    exit(0);
  }

  dprintf(1, "testdaemon: grandchild daemon (pid %d, ppid %d)\n", getpid(), getppid());

  /* Redirect stdio to /dev/null */
  {
    int nullfd;

    nullfd = open("/dev/null", O_RDONLY);
    if(nullfd >= 0){
      close(0);
      dup(nullfd);
      close(nullfd);
    }
    nullfd = open("/dev/null", O_WRONLY);
    if(nullfd >= 0){
      close(1);
      dup(nullfd);
      close(2);
      dup(nullfd);
      close(nullfd);
    }
  }

  /* Write to a log file instead since stdio is redirected */
  {
    int logfd;

    logfd = open("/tmp/testdaemon.log", O_CREATE | O_WRONLY | O_APPEND);
    if(logfd >= 0){
      char msg[64];
      int len;

      len = 0;
      msg[len++] = 't';
      msg[len++] = 'e';
      msg[len++] = 's';
      msg[len++] = 't';
      msg[len++] = 'd';
      msg[len++] = 'a';
      msg[len++] = 'e';
      msg[len++] = 'm';
      msg[len++] = 'o';
      msg[len++] = 'n';
      msg[len++] = ':';
      msg[len++] = ' ';
      msg[len++] = 'd';
      msg[len++] = 'a';
      msg[len++] = 'e';
      msg[len++] = 'm';
      msg[len++] = 'o';
      msg[len++] = 'n';
      msg[len++] = ' ';
      msg[len++] = 'r';
      msg[len++] = 'u';
      msg[len++] = 'n';
      msg[len++] = 'n';
      msg[len++] = 'i';
      msg[len++] = 'n';
      msg[len++] = 'g';
      msg[len++] = '\n';
      write(logfd, msg, len);

      /* Run for 30 seconds, writing periodic status */
      for(i = 0; i < 30; i++){
        sleep(1);
        write(logfd, ".", 1);
      }

      msg[0] = '\n';
      write(logfd, msg, 1);
      write(logfd, "testdaemon: exiting normally\n", 30);
      close(logfd);
    }
  }

  exit(0);
}
