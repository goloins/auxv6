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
/*
 * sys_errlist / sys_nerr — POSIX legacy error string table.
 * Indexed by errno value; entries for gaps are "Unknown error N".
 * sys_nerr is the size of the table (highest valid index + 1).
 */
#define _SYS_NERR 41

const char * const sys_errlist[_SYS_NERR] = {
  /* 0  */ "Success",
  /* 1  */ "Operation not permitted",
  /* 2  */ "No such file or directory",
  /* 3  */ "No such process",
  /* 4  */ "Interrupted system call",
  /* 5  */ "I/O error",
  /* 6  */ "No such device or address",
  /* 7  */ "Argument list too long",
  /* 8  */ "Exec format error",
  /* 9  */ "Bad file descriptor",
  /* 10 */ "No child processes",
  /* 11 */ "Try again",
  /* 12 */ "Out of memory",
  /* 13 */ "Permission denied",
  /* 14 */ "Bad address",
  /* 15 */ "Unknown error 15",
  /* 16 */ "Device or resource busy",
  /* 17 */ "File exists",
  /* 18 */ "Cross-device link",
  /* 19 */ "No such device",
  /* 20 */ "Not a directory",
  /* 21 */ "Is a directory",
  /* 22 */ "Invalid argument",
  /* 23 */ "Unknown error 23",
  /* 24 */ "Too many open files",
  /* 25 */ "Not a typewriter",
  /* 26 */ "Unknown error 26",
  /* 27 */ "Unknown error 27",
  /* 28 */ "No space left on device",
  /* 29 */ "Unknown error 29",
  /* 30 */ "Unknown error 30",
  /* 31 */ "Unknown error 31",
  /* 32 */ "Broken pipe",
  /* 33 */ "Domain error",
  /* 34 */ "Result too large",
  /* 35 */ "Unknown error 35",
  /* 36 */ "File name too long",
  /* 37 */ "Unknown error 37",
  /* 38 */ "Unknown error 38",
  /* 39 */ "Directory not empty",
  /* 40 */ "Too many symbolic links",
};

int sys_nerr = _SYS_NERR;
