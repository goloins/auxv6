/*
 * errstr.c - errno storage and error-description helpers split out of user/ulib.c
 */

#include "errno.h"
#include "string.h"

int errno = 0;

char*
strerror(int errnum)
{
  static char buf[32];

  switch(errnum) {
  case 1:  return "Operation not permitted";
  case 2:  return "No such file or directory";
  case 3:  return "No such process";
  case 4:  return "Interrupted system call";
  case 5:  return "I/O error";
  case 6:  return "No such device or address";
  case 7:  return "Argument list too long";
  case 8:  return "Exec format error";
  case 9:  return "Bad file descriptor";
  case 10: return "No child processes";
  case 11: return "Try again";
  case 12: return "Out of memory";
  case 13: return "Permission denied";
  case 14: return "Bad address";
  case 16: return "Device or resource busy";
  case 17: return "File exists";
  case 18: return "Cross-device link";
  case 19: return "No such device";
  case 20: return "Not a directory";
  case 21: return "Is a directory";
  case 22: return "Invalid argument";
  case 24: return "Too many open files";
  case 25: return "Not a typewriter";
  case 28: return "No space left on device";
  case 36: return "File name too long";
  case 39: return "Directory not empty";
  case 40: return "Too many symbolic links";
  default:
    {
      int n;
      int i;
      static const char pfx[] = "Unknown error ";

      n = errnum < 0 ? -errnum : errnum;
      i = 31;
      buf[i] = '\0';
      if(n == 0) {
        buf[--i] = '0';
      } else {
        while(n) {
          buf[--i] = '0' + n % 10;
          n /= 10;
        }
      }
      i -= sizeof(pfx) - 1;
      if(i < 0)
        i = 0;
      memmove(buf + i, pfx, sizeof(pfx) - 1);
      return buf + i;
    }
  }
}

char*
strerror_r(int errnum, char *buf, size_t buflen)
{
  const char *s;

  s = strerror(errnum);
  strlcpy(buf, s, buflen);
  return buf;
}

char*
strsignal(int sig)
{
  static char buf[32];

  switch(sig) {
  case 1:  return "Hangup";
  case 2:  return "Interrupt";
  case 3:  return "Quit";
  case 4:  return "Illegal instruction";
  case 6:  return "Aborted";
  case 7:  return "Bus error";
  case 8:  return "Floating point exception";
  case 9:  return "Killed";
  case 11: return "Segmentation fault";
  case 13: return "Broken pipe";
  case 14: return "Alarm clock";
  case 15: return "Terminated";
  case 17: return "Child status changed";
  case 18: return "Continued";
  case 19: return "Stopped (signal)";
  case 20: return "Stopped";
  default:
    {
      int n;
      int i;
      static const char pfx[] = "Signal ";

      n = sig;
      i = 31;
      buf[i] = '\0';
      if(n <= 0)
        n = 0;
      if(n == 0) {
        buf[--i] = '0';
      } else {
        while(n) {
          buf[--i] = '0' + n % 10;
          n /= 10;
        }
      }
      i -= sizeof(pfx) - 1;
      if(i < 0)
        i = 0;
      memmove(buf + i, pfx, sizeof(pfx) - 1);
      return buf + i;
    }
  }
}