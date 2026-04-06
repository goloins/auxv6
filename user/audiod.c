#include "types.h"
#include "fcntl.h"
#include "poll.h"
#include "signal.h"
#include "sys/ioctl.h"
#include "audio.h"
#include "audio_ioctl.h"
#include "auxv6/user.h"

#define DEFAULT_PCM_PATH "/dev/pcmC0D0p"
#define DEFAULT_CTL_PATH "/tmp/audiod.ctl"
#define DEFAULT_WRITE_CHUNK 512
#define DEFAULT_POLL_TIMEOUT_MS 250
#define MAX_WRITE_CHUNK 4096
#define CTRL_MAX 192

#define AUDIOD_MAX_TRACKS  8
#define TRACK_PATH_MAX    128

struct track {
  int     active;
  int     src_fd;
  int     loop;
  uint8_t gain_shift;
  char    path[TRACK_PATH_MAX];
};

static struct track tracks[AUDIOD_MAX_TRACKS];

static volatile sig_atomic_t keep_running = 1;

static void
usage(void)
{
  dprintf(2,
          "usage: audiod [-f] [-v] [-d pcm-dev] [-C ctl-path] [-r rate] [-c channels] [-F format]\\n"
          "              [-p period_frames] [-n periods] [-b buffer_frames]\\n"
          "              [-w write_bytes] [-t poll_timeout_ms]\\n"
          "\\n"
          "control commands via %s: status | set <r> <c> <f> <p> <n> <b> | set-write <bytes> | set-timeout <ms>\\n"
          "defaults: dev=%s rate=%d ch=%d fmt=%d period=%d periods=%d buffer=%d write=%d timeout=%d\\n",
          DEFAULT_CTL_PATH,
          DEFAULT_PCM_PATH,
          AUDIO_DEFAULT_RATE_HZ,
          AUDIO_DEFAULT_CHANNELS,
          AUDIO_FMT_S16_LE,
          AUDIO_DEFAULT_PERIOD_FRAMES,
          AUDIO_DEFAULT_PERIODS,
          AUDIO_DEFAULT_BUFFER_FRAMES,
          DEFAULT_WRITE_CHUNK,
          DEFAULT_POLL_TIMEOUT_MS);
  exit(1);
}

static void
on_term(int signo)
{
  if(signo == SIGTERM || signo == SIGINT)
    keep_running = 0;
}

static int
daemonize_self(void)
{
  int pid;
  int fd;

  pid = fork();
  if(pid < 0)
    return -1;
  if(pid > 0)
    exit(0);

  if(setsid() < 0)
    return -1;

  pid = fork();
  if(pid < 0)
    return -1;
  if(pid > 0)
    exit(0);

  chdir("/");

  close(0);
  close(1);
  close(2);

  fd = open("/dev/null", O_RDWR);
  if(fd < 0)
    return -1;

  if(fd != 0){
    if(dup2(fd, 0) < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0){
      close(fd);
      return -1;
    }
    close(fd);
  } else {
    if(dup(0) < 0 || dup(0) < 0)
      return -1;
  }

  return 0;
}

static int
configure_stream(int fd, struct audio_stream_params *params)
{
  if(ioctl(fd, AUDIO_IOC_SET_PARAMS, params) < 0)
    return -1;
  if(ioctl(fd, AUDIO_IOC_PREPARE, 0) < 0)
    return -1;
  if(ioctl(fd, AUDIO_IOC_START, 0) < 0)
    return -1;
  return 0;
}

static int
recover_stream(int fd)
{
  if(ioctl(fd, AUDIO_IOC_RESET_XRUN, 0) < 0)
    return -1;
  if(ioctl(fd, AUDIO_IOC_PREPARE, 0) < 0)
    return -1;
  if(ioctl(fd, AUDIO_IOC_START, 0) < 0)
    return -1;
  return 0;
}

static int
reconfigure_stream(int fd, struct audio_stream_params *params)
{
  if(ioctl(fd, AUDIO_IOC_STOP, 0) < 0)
    return -1;
  if(ioctl(fd, AUDIO_IOC_SET_PARAMS, params) < 0)
    return -1;
  if(ioctl(fd, AUDIO_IOC_PREPARE, 0) < 0)
    return -1;
  if(ioctl(fd, AUDIO_IOC_START, 0) < 0)
    return -1;
  return 0;
}

static void
tracks_init(void)
{
  int i;
  for(i = 0; i < AUDIOD_MAX_TRACKS; i++){
    tracks[i].active    = 0;
    tracks[i].src_fd    = -1;
    tracks[i].loop      = 0;
    tracks[i].gain_shift = 0;
    tracks[i].path[0]   = 0;
  }
}

static void
tracks_close_all(void)
{
  int i;
  for(i = 0; i < AUDIOD_MAX_TRACKS; i++){
    if(tracks[i].src_fd >= 0){
      close(tracks[i].src_fd);
      tracks[i].src_fd = -1;
    }
    tracks[i].active = 0;
  }
}

/* Read n bytes from track t.  Returns bytes read; deactivates slot on EOF. */
static int
track_read_chunk(struct track *t, uint8_t *buf, int n)
{
  int got;
  int total;

  if(t->src_fd < 0)
    return 0;

  total = 0;
  while(total < n){
    got = read(t->src_fd, buf + total, n - total);
    if(got < 0){
      close(t->src_fd);
      t->src_fd = -1;
      t->active = 0;
      return total;
    }
    if(got == 0){
      if(t->loop){
        lseek(t->src_fd, 0, SEEK_SET);
        continue;
      }
      close(t->src_fd);
      t->src_fd = -1;
      t->active = 0;
      return total;
    }
    total += got;
  }
  return total;
}

/*
 * Mix all active tracks (S16_LE only) into outbuf and write to hw_fd.
 * Falls back to writing silence for other formats or when no tracks active.
 */
static int
mix_write_hw(int hw_fd, int write_bytes, int sample_fmt, uint8_t *outbuf,
             uint8_t *inbuf, int32_t *mix_acc)
{
  int i;
  int j;
  int ns;
  int got;
  int any;
  int32_t v;
  int16_t s;

  if(sample_fmt != AUDIO_FMT_S16_LE || write_bytes < 2){
    memset(outbuf, 0, write_bytes);
    return write(hw_fd, outbuf, write_bytes);
  }

  ns = write_bytes / 2;

  any = 0;
  for(i = 0; i < AUDIOD_MAX_TRACKS; i++)
    if(tracks[i].active && tracks[i].src_fd >= 0)
      any = 1;

  if(!any){
    memset(outbuf, 0, write_bytes);
    return write(hw_fd, outbuf, write_bytes);
  }

  memset(mix_acc, 0, sizeof(int32_t) * (size_t)ns);

  for(i = 0; i < AUDIOD_MAX_TRACKS; i++){
    if(!tracks[i].active || tracks[i].src_fd < 0)
      continue;
    got = track_read_chunk(&tracks[i], inbuf, write_bytes);
    for(j = 0; j < got / 2; j++){
      s = (int16_t)((uint16_t)inbuf[2*j] | ((uint16_t)inbuf[2*j+1] << 8));
      mix_acc[j] += (int32_t)s >> tracks[i].gain_shift;
    }
  }

  for(i = 0; i < ns; i++){
    v = mix_acc[i];
    if(v >  32767) v =  32767;
    if(v < -32768) v = -32768;
    outbuf[2*i]   = (uint8_t)((uint32_t)v & 0xFF);
    outbuf[2*i+1] = (uint8_t)(((uint32_t)v >> 8) & 0xFF);
  }

  return write(hw_fd, outbuf, write_bytes);
}

static char*
next_tok(char **pp)
{
  char *p;
  char *tok;

  p = *pp;
  while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    p++;
  if(*p == 0){
    *pp = p;
    return 0;
  }

  tok = p;
  while(*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
    p++;
  if(*p){
    *p = 0;
    p++;
  }
  *pp = p;
  return tok;
}

static int
parse_int_tok(char **pp, int *out)
{
  char *t;
  int neg;
  int v;

  t = next_tok(pp);
  if(t == 0)
    return -1;

  neg = 0;
  if(*t == '-'){
    neg = 1;
    t++;
  }
  if(*t == 0)
    return -1;

  v = 0;
  while(*t){
    if(*t < '0' || *t > '9')
      return -1;
    v = v * 10 + (*t - '0');
    t++;
  }
  *out = neg ? -v : v;
  return 0;
}

static void
emit_status(int fd, int writes, int recoveries, struct audio_stream_params *params)
{
  struct audio_stream_status st;
  int i;

  memset(&st, 0, sizeof(st));
  st.abi_version = AUDIO_ABI_VERSION;
  st.struct_size = sizeof(st);
  if(ioctl(fd, AUDIO_IOC_GET_STATUS, &st) < 0){
    dprintf(2, "audiod: control status ioctl failed\n");
    return;
  }

  dprintf(1,
          "audiod: status state=%d writes=%d recoveries=%d queued=%d hw=%d sw=%d xruns=%d cfg(rate=%d ch=%d fmt=%d period=%d periods=%d buffer=%d)\n",
          st.state,
          writes,
          recoveries,
          (int)st.queued_frames,
          (int)st.hw_ptr_frames,
          (int)st.sw_ptr_frames,
          st.xruns,
          (int)params->sample_rate,
          (int)params->channels,
          (int)params->sample_format,
          (int)params->period_frames,
          (int)params->periods,
          (int)params->buffer_frames);

  for(i = 0; i < AUDIOD_MAX_TRACKS; i++){
    if(tracks[i].active)
      dprintf(1, "audiod:   track[%d] src=%s loop=%d gain_shift=%d\n",
              i, tracks[i].path, tracks[i].loop, (int)tracks[i].gain_shift);
  }
}

static void
control_try_apply(const char *ctl_path, int fd,
                  struct audio_stream_params *params,
                  int *write_bytes, int *timeout_ms,
                  int writes, int recoveries, int verbose)
{
  char raw[CTRL_MAX];
  char *p;
  char *cmd;
  int cfd;
  int n;
  int rate, channels, fmt, period_frames, periods, buffer_frames;
  int v;

  cfd = open(ctl_path, O_RDONLY);
  if(cfd < 0)
    return;

  n = read(cfd, raw, sizeof(raw) - 1);
  close(cfd);
  unlink(ctl_path);
  if(n <= 0)
    return;

  raw[n] = 0;
  p = raw;
  cmd = next_tok(&p);
  if(cmd == 0)
    return;

  if(strcmp(cmd, "status") == 0){
    emit_status(fd, writes, recoveries, params);
    return;
  }

  if(strcmp(cmd, "set") == 0){
    if(parse_int_tok(&p, &rate) < 0 ||
       parse_int_tok(&p, &channels) < 0 ||
       parse_int_tok(&p, &fmt) < 0 ||
       parse_int_tok(&p, &period_frames) < 0 ||
       parse_int_tok(&p, &periods) < 0 ||
       parse_int_tok(&p, &buffer_frames) < 0){
      dprintf(2, "audiod: control malformed set command\n");
      return;
    }
    if(rate <= 0 || channels <= 0 || period_frames <= 0 || periods <= 0 || buffer_frames <= 0){
      dprintf(2, "audiod: control rejected invalid stream params\n");
      return;
    }
    params->sample_rate = (uint32_t)rate;
    params->channels = (uint16_t)channels;
    params->sample_format = (uint16_t)fmt;
    params->period_frames = (uint32_t)period_frames;
    params->periods = (uint16_t)periods;
    params->buffer_frames = (uint32_t)buffer_frames;
    if(reconfigure_stream(fd, params) < 0){
      dprintf(2, "audiod: control stream reconfigure failed\n");
      return;
    }
    dprintf(1, "audiod: control applied stream set (%d %d %d %d %d %d)\n",
            rate, channels, fmt, period_frames, periods, buffer_frames);
    return;
  }

  if(strcmp(cmd, "set-write") == 0){
    if(parse_int_tok(&p, &v) < 0 || v <= 0 || v > MAX_WRITE_CHUNK){
      dprintf(2, "audiod: control rejected set-write value\n");
      return;
    }
    *write_bytes = v;
    if(verbose)
      dprintf(1, "audiod: control write_bytes=%d\n", *write_bytes);
    return;
  }

  if(strcmp(cmd, "set-timeout") == 0){
    if(parse_int_tok(&p, &v) < 0 || v < -1){
      dprintf(2, "audiod: control rejected set-timeout value\n");
      return;
    }
    *timeout_ms = v;
    if(verbose)
      dprintf(1, "audiod: control timeout_ms=%d\n", *timeout_ms);
    return;
  }

  if(strcmp(cmd, "track-load") == 0 || strcmp(cmd, "track-loop") == 0){
    int slot;
    char *pathp;
    int do_loop;
    int tfd;

    do_loop = strcmp(cmd, "track-loop") == 0;
    if(parse_int_tok(&p, &slot) < 0 || slot < 0 || slot >= AUDIOD_MAX_TRACKS){
      dprintf(2, "audiod: control %s: invalid slot\n", cmd);
      return;
    }
    pathp = next_tok(&p);
    if(pathp == 0 || pathp[0] == 0){
      dprintf(2, "audiod: control %s: missing path\n", cmd);
      return;
    }
    tfd = open(pathp, O_RDONLY);
    if(tfd < 0){
      dprintf(2, "audiod: control %s: cannot open %s\n", cmd, pathp);
      return;
    }
    if(tracks[slot].src_fd >= 0)
      close(tracks[slot].src_fd);
    tracks[slot].active    = 1;
    tracks[slot].src_fd    = tfd;
    tracks[slot].loop      = do_loop;
    tracks[slot].gain_shift = 0;
    memmove(tracks[slot].path, pathp, TRACK_PATH_MAX - 1);
    tracks[slot].path[TRACK_PATH_MAX - 1] = 0;
    if(verbose)
      dprintf(1, "audiod: track[%d] loaded %s loop=%d\n", slot, pathp, do_loop);
    return;
  }

  if(strcmp(cmd, "track-stop") == 0){
    int slot;
    if(parse_int_tok(&p, &slot) < 0 || slot < 0 || slot >= AUDIOD_MAX_TRACKS){
      dprintf(2, "audiod: control track-stop: invalid slot\n");
      return;
    }
    if(tracks[slot].src_fd >= 0){
      close(tracks[slot].src_fd);
      tracks[slot].src_fd = -1;
    }
    tracks[slot].active = 0;
    if(verbose)
      dprintf(1, "audiod: track[%d] stopped\n", slot);
    return;
  }

  if(strcmp(cmd, "track-gain") == 0){
    int slot;
    int shift;
    if(parse_int_tok(&p, &slot) < 0 || slot < 0 || slot >= AUDIOD_MAX_TRACKS){
      dprintf(2, "audiod: control track-gain: invalid slot\n");
      return;
    }
    if(parse_int_tok(&p, &shift) < 0 || shift < 0 || shift > 15){
      dprintf(2, "audiod: control track-gain: invalid shift (0-15)\n");
      return;
    }
    tracks[slot].gain_shift = (uint8_t)shift;
    if(verbose)
      dprintf(1, "audiod: track[%d] gain_shift=%d\n", slot, shift);
    return;
  }

  dprintf(2, "audiod: control unknown command '%s'\n", cmd);
}

int
main(int argc, char **argv)
{
  const char *devpath;
  const char *ctl_path;
  struct audio_stream_params params;
  struct sigaction sa;
  struct pollfd pfd;
  struct audio_stream_status st;
  uchar writebuf[MAX_WRITE_CHUNK];
  uint8_t *mix_inbuf;
  int32_t *mix_acc;
  int fd;
  int foreground;
  int verbose;
  int write_bytes;
  int timeout_ms;
  int writes;
  int recoveries;
  int i;
  int rc;

  devpath = DEFAULT_PCM_PATH;
  ctl_path = DEFAULT_CTL_PATH;
  foreground = 0;
  verbose = 0;
  write_bytes = DEFAULT_WRITE_CHUNK;
  timeout_ms = DEFAULT_POLL_TIMEOUT_MS;
  writes = 0;
  recoveries = 0;
  mix_inbuf = 0;
  mix_acc = 0;

  memset(&params, 0, sizeof(params));
  params.abi_version = AUDIO_ABI_VERSION;
  params.struct_size = sizeof(params);
  params.sample_rate = AUDIO_DEFAULT_RATE_HZ;
  params.channels = AUDIO_DEFAULT_CHANNELS;
  params.sample_format = AUDIO_FMT_S16_LE;
  params.period_frames = AUDIO_DEFAULT_PERIOD_FRAMES;
  params.periods = AUDIO_DEFAULT_PERIODS;
  params.buffer_frames = AUDIO_DEFAULT_BUFFER_FRAMES;

  for(i = 1; i < argc; i++){
    if(strcmp(argv[i], "-f") == 0){
      foreground = 1;
    } else if(strcmp(argv[i], "-v") == 0){
      verbose = 1;
    } else if(strcmp(argv[i], "-d") == 0){
      if(++i >= argc)
        usage();
      devpath = argv[i];
    } else if(strcmp(argv[i], "-C") == 0){
      if(++i >= argc)
        usage();
      ctl_path = argv[i];
    } else if(strcmp(argv[i], "-r") == 0){
      if(++i >= argc)
        usage();
      params.sample_rate = (uint32_t)atoi(argv[i]);
    } else if(strcmp(argv[i], "-c") == 0){
      if(++i >= argc)
        usage();
      params.channels = (uint16_t)atoi(argv[i]);
    } else if(strcmp(argv[i], "-F") == 0){
      if(++i >= argc)
        usage();
      params.sample_format = (uint16_t)atoi(argv[i]);
    } else if(strcmp(argv[i], "-p") == 0){
      if(++i >= argc)
        usage();
      params.period_frames = (uint32_t)atoi(argv[i]);
    } else if(strcmp(argv[i], "-n") == 0){
      if(++i >= argc)
        usage();
      params.periods = (uint16_t)atoi(argv[i]);
    } else if(strcmp(argv[i], "-b") == 0){
      if(++i >= argc)
        usage();
      params.buffer_frames = (uint32_t)atoi(argv[i]);
    } else if(strcmp(argv[i], "-w") == 0){
      if(++i >= argc)
        usage();
      write_bytes = atoi(argv[i]);
    } else if(strcmp(argv[i], "-t") == 0){
      if(++i >= argc)
        usage();
      timeout_ms = atoi(argv[i]);
    } else {
      usage();
    }
  }

  if(params.sample_rate == 0 || params.channels == 0 || params.period_frames == 0 ||
     params.periods == 0 || write_bytes <= 0 || write_bytes > MAX_WRITE_CHUNK ||
     timeout_ms < -1)
    usage();

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_term;
  sigemptyset(&sa.sa_mask);
  if(sigaction(SIGTERM, &sa, 0) < 0 || sigaction(SIGINT, &sa, 0) < 0){
    dprintf(2, "audiod: failed to install signal handlers\n");
    exit(1);
  }

  if(!foreground && daemonize_self() < 0)
    exit(1);

  tracks_init();
  mix_inbuf = (uint8_t*)malloc(MAX_WRITE_CHUNK);
  mix_acc = (int32_t*)malloc(sizeof(int32_t) * (MAX_WRITE_CHUNK / 2));
  if(mix_inbuf == 0 || mix_acc == 0){
    dprintf(2, "audiod: mixer buffer alloc failed\n");
    if(mix_inbuf)
      free(mix_inbuf);
    if(mix_acc)
      free(mix_acc);
    exit(1);
  }

  fd = open(devpath, O_RDWR);
  if(fd < 0){
    dprintf(2, "audiod: open %s failed\n", devpath);
    free(mix_inbuf);
    free(mix_acc);
    exit(1);
  }

  if(configure_stream(fd, &params) < 0){
    dprintf(2, "audiod: failed to configure/start stream on %s\n", devpath);
    close(fd);
    free(mix_inbuf);
    free(mix_acc);
    exit(1);
  }

  memset(writebuf, 0, sizeof(writebuf));
  memset(&pfd, 0, sizeof(pfd));
  pfd.fd = fd;
  pfd.events = POLLOUT;

  if(verbose){
    dprintf(1,
            "audiod: started on %s ctl=%s (rate=%d ch=%d fmt=%d period=%d periods=%d buffer=%d write=%d timeout=%d)\n",
            devpath,
            ctl_path,
            params.sample_rate,
            params.channels,
            params.sample_format,
            params.period_frames,
            params.periods,
            params.buffer_frames,
            write_bytes,
            timeout_ms);
  }

  rc = 0;
  while(keep_running){
    int pret;
    int wn;

    control_try_apply(ctl_path, fd, &params,
                      &write_bytes, &timeout_ms,
                      writes, recoveries, verbose);

    pfd.revents = 0;
    pret = poll(&pfd, 1, timeout_ms);
    if(pret < 0){
      if(verbose)
        dprintf(2, "audiod: poll failed\n");
      rc = 1;
      break;
    }
    if(pret == 0)
      continue;

    if(pfd.revents & POLLERR){
      if(recover_stream(fd) < 0){
        dprintf(2, "audiod: xrun recovery failed\n");
        rc = 1;
        break;
      }
      recoveries++;
      continue;
    }

    if(!(pfd.revents & POLLOUT))
      continue;

    wn = mix_write_hw(fd, write_bytes, (int)params.sample_format, writebuf,
              mix_inbuf, mix_acc);
    if(wn < 0){
      memset(&st, 0, sizeof(st));
      st.abi_version = AUDIO_ABI_VERSION;
      st.struct_size = sizeof(st);
      if(ioctl(fd, AUDIO_IOC_GET_STATUS, &st) >= 0 && st.state == AUDIO_ST_XRUN){
        if(recover_stream(fd) < 0){
          dprintf(2, "audiod: write-path xrun recovery failed\n");
          rc = 1;
          break;
        }
        recoveries++;
        continue;
      }
      dprintf(2, "audiod: write failed\n");
      rc = 1;
      break;
    }

    writes++;
    if(verbose && (writes % 512) == 0)
      dprintf(1, "audiod: writes=%d recoveries=%d\n", writes, recoveries);
  }

  (void)ioctl(fd, AUDIO_IOC_DRAIN, 0);
  (void)ioctl(fd, AUDIO_IOC_STOP, 0);
  tracks_close_all();
  close(fd);
  free(mix_inbuf);
  free(mix_acc);

  if(verbose)
    dprintf(1, "audiod: exiting (writes=%d recoveries=%d rc=%d)\n", writes, recoveries, rc);

  exit(rc);
}
