#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "auxv6/user.h"

#define DEFAULT_OUT_PATH "/tmp/tone.raw"
#define DEFAULT_RATE 48000
#define DEFAULT_CHANNELS 2
#define DEFAULT_FREQ_HZ 440
#define DEFAULT_DURATION_MS 1000
#define DEFAULT_AMPLITUDE 12000
#define MAX_CHANNELS 8
#define CHUNK_FRAMES 128

static void
usage(void)
{
  dprintf(2,
          "usage: audiotone [-o out.raw] [-f hz] [-d ms] [-r rate] [-c channels] [-a amplitude]\n"
          "defaults: out=%s freq=%d duration_ms=%d rate=%d channels=%d amplitude=%d\n",
          DEFAULT_OUT_PATH,
          DEFAULT_FREQ_HZ,
          DEFAULT_DURATION_MS,
          DEFAULT_RATE,
          DEFAULT_CHANNELS,
          DEFAULT_AMPLITUDE);
  exit(1);
}

static int
write_all(int fd, const char *buf, int n)
{
  int off;
  int wn;

  off = 0;
  while(off < n){
    wn = write(fd, buf + off, n - off);
    if(wn <= 0)
      return -1;
    off += wn;
  }
  return 0;
}

int
main(int argc, char **argv)
{
  const char *outpath;
  int rate;
  int channels;
  int freq;
  int duration_ms;
  int amplitude;
  int outfd;
  int total_frames;
  int period;
  int half_period;
  int produced;
  int16_t samples[CHUNK_FRAMES * MAX_CHANNELS];
  int i;

  outpath = DEFAULT_OUT_PATH;
  rate = DEFAULT_RATE;
  channels = DEFAULT_CHANNELS;
  freq = DEFAULT_FREQ_HZ;
  duration_ms = DEFAULT_DURATION_MS;
  amplitude = DEFAULT_AMPLITUDE;

  for(i = 1; i < argc; i++){
    if(strcmp(argv[i], "-o") == 0){
      if(++i >= argc)
        usage();
      outpath = argv[i];
    } else if(strcmp(argv[i], "-f") == 0){
      if(++i >= argc)
        usage();
      freq = atoi(argv[i]);
    } else if(strcmp(argv[i], "-d") == 0){
      if(++i >= argc)
        usage();
      duration_ms = atoi(argv[i]);
    } else if(strcmp(argv[i], "-r") == 0){
      if(++i >= argc)
        usage();
      rate = atoi(argv[i]);
    } else if(strcmp(argv[i], "-c") == 0){
      if(++i >= argc)
        usage();
      channels = atoi(argv[i]);
    } else if(strcmp(argv[i], "-a") == 0){
      if(++i >= argc)
        usage();
      amplitude = atoi(argv[i]);
    } else {
      usage();
    }
  }

  if(rate <= 0 || freq <= 0 || duration_ms <= 0 || channels <= 0 ||
     channels > MAX_CHANNELS || amplitude <= 0 || amplitude > 32767)
    usage();

  if(strcmp(outpath, "-") == 0){
    outfd = 1;
  } else {
    struct stat st;
    /* Try plain O_WRONLY first (works for device nodes and existing files).
     * Fall back to O_CREATE|O_WRONLY|O_TRUNC if the file does not exist. */
    outfd = open(outpath, O_WRONLY);
    if(outfd < 0){
      if(stat(outpath, &st) < 0)
        outfd = open(outpath, O_CREATE | O_WRONLY | O_TRUNC);
    }
    if(outfd < 0){
      dprintf(2, "audiotone: cannot open %s\n", outpath);
      exit(1);
    }
  }

  total_frames = (rate * duration_ms) / 1000;
  period = rate / freq;
  if(period <= 0)
    period = 1;
  half_period = period / 2;
  if(half_period <= 0)
    half_period = 1;

  produced = 0;
  while(produced < total_frames){
    int frames;
    int s;
    int f;
    int ch;

    frames = total_frames - produced;
    if(frames > CHUNK_FRAMES)
      frames = CHUNK_FRAMES;

    for(f = 0; f < frames; f++){
      int phase;

      phase = (produced + f) % period;
      s = (phase < half_period) ? amplitude : -amplitude;
      for(ch = 0; ch < channels; ch++)
        samples[f * channels + ch] = (int16_t)s;
    }

    if(write_all(outfd, (const char*)samples, frames * channels * (int)sizeof(int16_t)) < 0){
      dprintf(2, "audiotone: write failed\n");
      if(outfd != 1)
        close(outfd);
      exit(1);
    }

    produced += frames;
  }

  if(outfd != 1)
    close(outfd);

  dprintf(1,
          "audiotone: wrote %d frames (%d ms @ %d Hz, %d ch, %d Hz square) to %s\n",
          total_frames,
          duration_ms,
          rate,
          channels,
          freq,
          outpath);
  exit(0);
}
