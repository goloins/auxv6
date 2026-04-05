#include "types.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define PROC_AUDIO "/proc/audio"
#define PROC_AUDIO_STATS "/proc/audio_stats"
#define PROC_AUDIO_CLIENTS "/proc/audio_clients"

static int
dump_file(const char *path)
{
  int fd;
  int n;
  char buf[256];

  fd = open(path, O_RDONLY);
  if(fd < 0){
    dprintf(2, "audiostat: cannot open %s\n", path);
    return -1;
  }

  dprintf(1, "== %s ==\n", path);
  while((n = read(fd, buf, sizeof(buf))) > 0)
    write(1, buf, n);
  close(fd);

  if(n < 0){
    dprintf(2, "audiostat: read error on %s\n", path);
    return -1;
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  int rc;

  if(argc != 1){
    dprintf(2, "usage: audiostat\n");
    exit(1);
  }

  rc = 0;
  if(dump_file(PROC_AUDIO) < 0)
    rc = 1;
  if(dump_file(PROC_AUDIO_STATS) < 0)
    rc = 1;
  if(dump_file(PROC_AUDIO_CLIENTS) < 0)
    rc = 1;

  exit(rc);
}
