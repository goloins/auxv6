#include "types.h"
#include "date.h"
#include "fcntl.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "time.h"
#include "auxv6/user.h"

#define TZ_CONFIG  "/etc/timezone"
#define ZONES_TAB  "/usr/share/zoneinfo/zones.tab"

/*
 * Read the timezone name from /etc/timezone.
 * The file contains a single line with the timezone name (e.g. "UTC" or
 * "America/New_York"). Returns 0 on success, -1 on failure (buf is set to
 * "UTC" as a fallback in either case).
 */
static int
read_timezone(char *buf, int buflen)
{
  FILE *fp;
  char line[128];
  int len;

  snprintf(buf, buflen, "UTC");

  fp = fopen(TZ_CONFIG, "r");
  if(fp == 0)
    return -1;

  while(fgets(line, sizeof(line), fp) != 0) {
    /* strip trailing whitespace / newline */
    len = strlen(line);
    while(len > 0 &&
          (line[len-1] == '\n' || line[len-1] == '\r' ||
           line[len-1] == ' '  || line[len-1] == '\t'))
      line[--len] = '\0';

    /* skip blank lines and comments */
    if(len == 0 || line[0] == '#')
      continue;

    snprintf(buf, buflen, "%s", line);
    fclose(fp);
    return 0;
  }

  fclose(fp);
  return -1;
}

/*
 * Look up a timezone by name in /usr/share/zoneinfo/zones.tab.
 * Sets *offset_sec to the UTC offset in seconds and abbr to the short name.
 * Returns 0 on success, -1 if the zone is not found.
 */
static int
lookup_zone(const char *name, int *offset_sec, char *abbr, int abbr_len)
{
  FILE *fp;
  char line[256];
  char *f1, *f2, *f3;
  int len;

  *offset_sec = 0;
  snprintf(abbr, abbr_len, "UTC");

  fp = fopen(ZONES_TAB, "r");
  if(fp == 0)
    return -1;

  while(fgets(line, sizeof(line), fp) != 0) {
    /* strip trailing whitespace */
    len = strlen(line);
    while(len > 0 &&
          (line[len-1] == '\n' || line[len-1] == '\r' ||
           line[len-1] == ' '  || line[len-1] == '\t'))
      line[--len] = '\0';

    if(len == 0 || line[0] == '#')
      continue;

    /* fields separated by tab: name<TAB>offset_sec<TAB>abbr */
    f1 = line;
    f2 = strchr(f1, '\t');
    if(f2 == 0)
      continue;
    *f2++ = '\0';

    f3 = strchr(f2, '\t');
    if(f3 == 0)
      continue;
    *f3++ = '\0';

    if(strcmp(f1, name) != 0)
      continue;

    *offset_sec = atoi(f2);
    snprintf(abbr, abbr_len, "%s", f3);
    fclose(fp);
    return 0;
  }

  fclose(fp);
  return -1;
}

int
main(int argc, char *argv[])
{
  struct rtcdate r;
  struct tm utc_tm;
  struct tm local_tm;
  time_t epoch;
  time_t local_epoch;
  char tzname[64];
  char abbr[32];
  int offset_sec;
  int utc_flag;
  int i;

  utc_flag = 0;
  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--utc") == 0) {
      utc_flag = 1;
    } else {
      dprintf(2, "usage: date [-u]\n");
      exit(1);
    }
  }

  if(date(&r) < 0) {
    dprintf(2, "date: failed to read RTC\n");
    exit(1);
  }

  if(utc_flag) {
    dprintf(1, "%d-%02d-%02d %02d:%02d:%02d UTC\n",
           r.year, r.month, r.day, r.hour, r.minute, r.second);
    exit(0);
  }

  /* Determine local timezone and offset */
  read_timezone(tzname, sizeof(tzname));
  if(lookup_zone(tzname, &offset_sec, abbr, sizeof(abbr)) < 0) {
    /* Unknown zone: fall back silently to UTC */
    offset_sec = 0;
    snprintf(abbr, sizeof(abbr), "UTC");
  }

  if(offset_sec == 0) {
    /* No conversion needed */
    dprintf(1, "%d-%02d-%02d %02d:%02d:%02d %s\n",
           r.year, r.month, r.day, r.hour, r.minute, r.second, abbr);
    exit(0);
  }

  /* Convert RTC (UTC) to epoch, apply offset, convert back */
  utc_tm.tm_year  = (int)r.year - 1900;
  utc_tm.tm_mon   = (int)r.month - 1;
  utc_tm.tm_mday  = (int)r.day;
  utc_tm.tm_hour  = (int)r.hour;
  utc_tm.tm_min   = (int)r.minute;
  utc_tm.tm_sec   = (int)r.second;
  utc_tm.tm_isdst = 0;
  utc_tm.tm_wday  = 0;
  utc_tm.tm_yday  = 0;

  epoch = mktime(&utc_tm);
  local_epoch = epoch + (time_t)offset_sec;

  if(gmtime_r(&local_epoch, &local_tm) == 0) {
    dprintf(2, "date: time conversion failed\n");
    exit(1);
  }

  dprintf(1, "%d-%02d-%02d %02d:%02d:%02d %s\n",
         local_tm.tm_year + 1900,
         local_tm.tm_mon + 1,
         local_tm.tm_mday,
         local_tm.tm_hour,
         local_tm.tm_min,
         local_tm.tm_sec,
         abbr);
  exit(0);
}
