#include "types.h"
#include "fcntl.h"
#include "sys/ioctl.h"
#include "audio.h"
#include "auxv6/user.h"

#define AUDIOPCM_PATH "/dev/pcmC0D0p"

static void
usage(void)
{
  dprintf(2, "usage: audiotest [pcm-dev] [chunks] [chunk-bytes]\n");
  exit(1);
}

static int
set_default_params(int fd)
{
  struct audio_stream_params p;

  memset(&p, 0, sizeof(p));
  p.abi_version = AUDIO_ABI_VERSION;
  p.struct_size = sizeof(p);
  p.sample_rate = 48000;
  p.channels = 2;
  p.sample_format = AUDIO_FMT_S16_LE;
  p.period_frames = 256;
  p.periods = 4;
  p.buffer_frames = 1024;

  if(ioctl(fd, AUDIO_IOC_SET_PARAMS, &p) < 0)
    return -1;
  return 0;
}

int
main(int argc, char *argv[])
{
  const char *path;
  int chunks;
  int chunk_bytes;
  int fd;
  int i;
  int j;
  int n;
  int xruns;
  uint8_t buf[1024];
  struct audio_stream_status st;

  path = AUDIOPCM_PATH;
  chunks = 128;
  chunk_bytes = 512;

  if(argc > 4)
    usage();
  if(argc >= 2)
    path = argv[1];
  if(argc >= 3)
    chunks = atoi(argv[2]);
  if(argc >= 4)
    chunk_bytes = atoi(argv[3]);

  if(chunks <= 0 || chunk_bytes <= 0 || chunk_bytes > (int)sizeof(buf))
    usage();

  fd = open(path, O_RDWR);
  if(fd < 0){
    dprintf(2, "audiotest: cannot open %s\n", path);
    exit(1);
  }

  if(set_default_params(fd) < 0){
    dprintf(2, "audiotest: AUDIO_IOC_SET_PARAMS failed\n");
    close(fd);
    exit(1);
  }

  if(ioctl(fd, AUDIO_IOC_PREPARE, 0) < 0){
    dprintf(2, "audiotest: AUDIO_IOC_PREPARE failed\n");
    close(fd);
    exit(1);
  }

  xruns = 0;
  for(i = 0; i < chunks; i++){
    for(j = 0; j < chunk_bytes; j++)
      buf[j] = (uint8_t)((i + j) & 0xff);

    n = write(fd, (char*)buf, chunk_bytes);
    if(n < 0){
      xruns++;
      if(ioctl(fd, AUDIO_IOC_RESET_XRUN, 0) < 0 ||
         ioctl(fd, AUDIO_IOC_PREPARE, 0) < 0){
        dprintf(2, "audiotest: xrun recovery failed at chunk %d\n", i);
        close(fd);
        exit(1);
      }
      continue;
    }

    if((i % 32) == 0){
      memset(&st, 0, sizeof(st));
      st.abi_version = AUDIO_ABI_VERSION;
      st.struct_size = sizeof(st);
      if(ioctl(fd, AUDIO_IOC_GET_STATUS, &st) == 0){
        dprintf(1, "chunk %d state=%d queued=%d xruns=%d\n",
                i, st.state, (uint)st.queued_frames, st.xruns);
      }
    }
  }

  (void)ioctl(fd, AUDIO_IOC_DRAIN, 0);
  memset(&st, 0, sizeof(st));
  st.abi_version = AUDIO_ABI_VERSION;
  st.struct_size = sizeof(st);
  if(ioctl(fd, AUDIO_IOC_GET_STATUS, &st) == 0){
    dprintf(1, "final state=%d queued=%d xruns=%d recoveries=%d\n",
            st.state, (uint)st.queued_frames, st.xruns, st.recoveries);
  }
  dprintf(1, "audiotest local_xruns=%d\n", xruns);

  close(fd);
  exit(0);
}
