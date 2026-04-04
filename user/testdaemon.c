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
  int logfd;

  dprintf(1, "testdaemon: starting (pid %d, ppid %d)\n", getpid(), getppid());

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

  dprintf(1, "testdaemon: first child (pid %d, ppid %d)\n", getpid(), getppid());

  /* Create new session */
  if(setsid() < 0){
    dprintf(2, "testdaemon: setsid failed\n");
    exit(1);
  }
  dprintf(1, "testdaemon: setsid ok, new sid (pid %d, ppid %d)\n", getpid(), getppid());

  /* Second fork */
  pid = fork();
  if(pid < 0){
    dprintf(2, "testdaemon: second fork failed\n");
    exit(1);
  }
  if(pid > 0){
    dprintf(1, "testdaemon: intermediate child exiting (pid %d -> grandchild %d)\n", getpid(), pid);
    exit(0);  /* This should trigger reparenting of grandchild to init */
  }

  /* Give the system a moment for reparenting to complete */
  sleep(1);

  dprintf(1, "testdaemon: grandchild daemon (pid %d, ppid %d) - should be ppid 1!\n", getpid(), getppid());

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
  logfd = open("/tmp/testdaemon.log", O_CREATE | O_WRONLY | O_APPEND);
  if(logfd >= 0){
    char msg[128];
    int len;

    /* Write startup message with final ppid */
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
    msg[len++] = 'p';
    msg[len++] = 'i';
    msg[len++] = 'd';
    msg[len++] = '=';

    /* Convert pid to string */
    {
      int p = getpid();
      int digits = 0;
      int temp = p;
      int start;
      while(temp > 0){
        digits++;
        temp /= 10;
      }
      start = len;
      while(digits > 0){
        msg[len + digits - 1] = '0' + (p % 10);
        p /= 10;
        digits--;
      }
      len += (digits > 0 ? digits : len - start);
    }

    msg[len++] = ' ';
    msg[len++] = 'p';
    msg[len++] = 'p';
    msg[len++] = 'i';
    msg[len++] = 'd';
    msg[len++] = '=';

    /* Convert ppid to string */
    {
      int p = getppid();
      int digits = 0;
      int temp = p;
      int start;
      while(temp > 0){
        digits++;
        temp /= 10;
      }
      start = len;
      while(digits > 0){
        msg[len + digits - 1] = '0' + (p % 10);
        p /= 10;
        digits--;
      }
      len += (digits > 0 ? digits : len - start);
    }

    msg[len++] = '\n';
    write(logfd, msg, len);

    /* Run for 10 seconds, writing periodic status */
    for(i = 0; i < 10; i++){
      sleep(1);
      write(logfd, ".", 1);
    }

    write(logfd, " done\n", 6);
    close(logfd);
  }

  exit(0);
}
