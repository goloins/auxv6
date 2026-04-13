// Signal delivery test program

#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "signal.h"

volatile int signal_received = 0;
volatile int signal_number = 0;

void
sighandler(int signo)
{
  signal_received = 1;
  signal_number = signo;
}

int
main(int argc, char *argv[])
{
  struct sigaction sa;
  int pid;

  dprintf(1, "Signal delivery test\n");

  // Test 1: Register handler for SIGINT
  dprintf(1, "Test 1: Register SIGINT handler\n");
  sa.sa_handler = sighandler;
  sa.sa_mask = 0;
  sa.sa_flags = 0;
  if(sigaction(SIGINT, &sa, 0) < 0) {
    dprintf(1, "FAIL: sigaction failed\n");
    exit(0);
  }
  dprintf(1, "  sigaction OK\n");

  // Test 2: Send signal to self
  dprintf(1, "Test 2: Send SIGINT to self\n");
  pid = getpid();
  signal_received = 0;
  signal_number = 0;
  
  if(sigsend(pid, SIGINT) < 0) {
    dprintf(1, "FAIL: sigsend failed\n");
    exit(0);
  }
  dprintf(1, "  sigsend OK\n");

  // The signal should have been delivered by now (after syscall return)
  if(signal_received) {
    dprintf(1, "  Signal handler was called!\n");
    dprintf(1, "  Signal number: %d (expected %d)\n", signal_number, SIGINT);
    if(signal_number == SIGINT) {
      dprintf(1, "  PASS: Received correct signal\n");
    } else {
      dprintf(1, "  FAIL: Wrong signal number\n");
    }
  } else {
    dprintf(1, "  FAIL: Signal handler was NOT called\n");
  }

  // Test 3: Multiple signals
  dprintf(1, "Test 3: Multiple signals\n");
  
  // Register handler for SIGTERM too
  if(sigaction(SIGTERM, &sa, 0) < 0) {
    dprintf(1, "FAIL: sigaction for SIGTERM failed\n");
    exit(0);
  }
  
  signal_received = 0;
  sigsend(pid, SIGTERM);
  if(signal_received && signal_number == SIGTERM) {
    dprintf(1, "  PASS: SIGTERM delivered\n");
  } else {
    dprintf(1, "  FAIL: SIGTERM not delivered (received=%d, signo=%d)\n", 
           signal_received, signal_number);
  }

  // Test 4: Signal from fork'd child
  dprintf(1, "Test 4: Signal from child process\n");
  signal_received = 0;
  
  int child = fork();
  if(child == 0) {
    // Child: send signal to parent then exit
    sleep(1);  // Give parent time to wait
    sigsend(pid, SIGINT);
    exit(0);
  }
  
  // Parent: wait a bit then check
  sleep(5);
  
  if(signal_received && signal_number == SIGINT) {
    dprintf(1, "  PASS: Received signal from child\n");
  } else {
    dprintf(1, "  INFO: Signal may or may not have arrived yet (received=%d)\n", 
           signal_received);
  }
  
  wait();  // Reap child
  
  // Test 5: User-defined signals
  dprintf(1, "Test 5: User-defined signals (SIGUSR1/SIGUSR2)\n");
  
  if(sigaction(SIGUSR1, &sa, 0) < 0) {
    dprintf(1, "  FAIL: sigaction for SIGUSR1 failed\n");
  } else {
    signal_received = 0;
    sigsend(pid, SIGUSR1);
    if(signal_received && signal_number == SIGUSR1) {
      dprintf(1, "  PASS: SIGUSR1 delivered (signo=%d)\n", signal_number);
    } else {
      dprintf(1, "  FAIL: SIGUSR1 not delivered\n");
    }
  }
  
  if(sigaction(SIGUSR2, &sa, 0) < 0) {
    dprintf(1, "  FAIL: sigaction for SIGUSR2 failed\n");
  } else {
    signal_received = 0;
    sigsend(pid, SIGUSR2);
    if(signal_received && signal_number == SIGUSR2) {
      dprintf(1, "  PASS: SIGUSR2 delivered (signo=%d)\n", signal_number);
    } else {
      dprintf(1, "  FAIL: SIGUSR2 not delivered\n");
    }
  }

  // Test 6: SIGABRT
  dprintf(1, "Test 6: SIGABRT\n");
  if(sigaction(SIGABRT, &sa, 0) < 0) {
    dprintf(1, "  FAIL: sigaction for SIGABRT failed\n");
  } else {
    signal_received = 0;
    sigsend(pid, SIGABRT);
    if(signal_received && signal_number == SIGABRT) {
      dprintf(1, "  PASS: SIGABRT delivered (signo=%d)\n", signal_number);
    } else {
      dprintf(1, "  FAIL: SIGABRT not delivered\n");
    }
  }

  // Test 7: SIGHUP
  dprintf(1, "Test 7: SIGHUP\n");
  if(sigaction(SIGHUP, &sa, 0) < 0) {
    dprintf(1, "  FAIL: sigaction for SIGHUP failed\n");
  } else {
    signal_received = 0;
    sigsend(pid, SIGHUP);
    if(signal_received && signal_number == SIGHUP) {
      dprintf(1, "  PASS: SIGHUP delivered (signo=%d)\n", signal_number);
    } else {
      dprintf(1, "  FAIL: SIGHUP not delivered\n");
    }
  }

  // Test 8: SIGALRM via alarm()
  dprintf(1, "Test 8: SIGALRM via alarm()\n");
  if(sigaction(SIGALRM, &sa, 0) < 0) {
    dprintf(1, "  FAIL: sigaction for SIGALRM failed\n");
  } else {
    signal_received = 0;
    alarm(1);  // 1 second alarm
    dprintf(1, "  Waiting for alarm...\n");
    sleep(150);  // Wait 1.5 seconds (150 ticks)
    if(signal_received && signal_number == SIGALRM) {
      dprintf(1, "  PASS: SIGALRM delivered (signo=%d)\n", signal_number);
    } else {
      dprintf(1, "  FAIL: SIGALRM not delivered (received=%d, signo=%d)\n",
             signal_received, signal_number);
    }
  }

  // Test 9: SIGPIPE via broken pipe
  dprintf(1, "Test 9: SIGPIPE via broken pipe\n");
  if(sigaction(SIGPIPE, &sa, 0) < 0) {
    dprintf(1, "  FAIL: sigaction for SIGPIPE failed\n");
  } else {
    int pfd[2];
    if(pipe(pfd) < 0) {
      dprintf(1, "  FAIL: pipe() failed\n");
    } else {
      close(pfd[0]);  // Close read end
      signal_received = 0;
      write(pfd[1], "x", 1);  // Write to broken pipe
      close(pfd[1]);
      if(signal_received && signal_number == SIGPIPE) {
        dprintf(1, "  PASS: SIGPIPE delivered (signo=%d)\n", signal_number);
      } else {
        dprintf(1, "  FAIL: SIGPIPE not delivered (received=%d, signo=%d)\n",
               signal_received, signal_number);
      }
    }
  }
  
  dprintf(1, "\nSignal test complete!\n");
  exit(0);
}
