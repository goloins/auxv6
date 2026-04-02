#include "types.h"
#include "date.h"
#include "auxv6/user.h"

int
main(int argc, char *argv[])
{
  struct rtcdate r;

  if(argc != 1){
    dprintf(2, "usage: date\n");
    exit(0);
  }

  if(date(&r) < 0){
    dprintf(2, "date: failed to read RTC\n");
    exit(0);
  }

  dprintf(1, "%d-%02d-%02d %02d:%02d:%02d UTC\n",
         r.year, r.month, r.day, r.hour, r.minute, r.second);
  exit(0);
}
