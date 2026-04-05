/*
 * audiopollstress - concurrent audio stream poll/write stress tool
 *
 * Opens N PCM stream fds, polls all of them simultaneously for POLLOUT
 * readiness, writes a chunk whenever a fd is ready, and validates that
 * readiness signals arrive coherently under concurrent write pressure.
 *
 * Exercises the kernel-side audio poll wakeup path (audio_poll_events),
 * the per-fd ring-space accounting, xrun detection/recovery, and the
 * F_SETFL O_NONBLOCK tracking added in Stage-1 tranche 3+.
 */

#include "types.h"
#include "fcntl.h"
#include "poll.h"
#include "audio.h"
#include "audio_ioctl.h"
#include "auxv6/user.h"

#define AUDIOPCM_DEFAULT  "/dev/pcmC0D0p"
#define MAX_STREAMS        16
#define DEFAULT_STREAMS     4
#define DEFAULT_CHUNKS     64
#define DEFAULT_CHUNK_BYTES 512
#define DEFAULT_TIMEOUT_MS  1000
#define WBUF_MAX            1024

static const char *devpath    = AUDIOPCM_DEFAULT;
static int         nstreams   = DEFAULT_STREAMS;
static int         total_chunks   = DEFAULT_CHUNKS;
static int         chunk_bytes    = DEFAULT_CHUNK_BYTES;
static int         timeout_ms     = DEFAULT_TIMEOUT_MS;
static int         verbose        = 0;
static int         test_nonblock  = 0;

struct sstate {
  int fd;
  int chunks_done;
  int poll_hits;     /* POLLOUT events received */
  int poll_errs;     /* POLLERR events received */
  int writes;        /* successful write calls */
  int write_errs;    /* write calls that returned -1 */
  int xruns;         /* streams that entered xrun state */
  int recoveries;    /* successful xrun recoveries */
  int failed;        /* nonzero if stream hit unrecoverable error */
};

static void
usage(void)
{
  dprintf(2,
    "usage: audiopollstress [options] [pcm-dev]\n"
    "\n"
    "options:\n"
    "  -n <streams>  concurrent stream fds (default %d, max %d)\n"
    "  -c <chunks>   total write chunks per stream (default %d)\n"
    "  -b <bytes>    bytes per write chunk (default %d, max %d)\n"
    "  -t <ms>       poll timeout in ms (default %d; -1 = infinite)\n"
    "  -N            toggle O_NONBLOCK via fcntl(F_SETFL) after configure\n"
    "  -v            verbose per-event output\n"
    "\n"
    "pcm-dev defaults to %s\n",
    DEFAULT_STREAMS, MAX_STREAMS,
    DEFAULT_CHUNKS,
    DEFAULT_CHUNK_BYTES, WBUF_MAX,
    DEFAULT_TIMEOUT_MS,
    AUDIOPCM_DEFAULT);
  exit(1);
}

static int
stream_open(const char *path)
{
  struct audio_stream_params p;
  int fd;

  fd = open(path, O_RDWR);
  if(fd < 0)
    return -1;

  memset(&p, 0, sizeof(p));
  p.abi_version   = AUDIO_ABI_VERSION;
  p.struct_size   = sizeof(p);
  p.sample_rate   = 48000;
  p.channels      = 2;
  p.sample_format = AUDIO_FMT_S16_LE;
  p.period_frames = 256;
  p.periods       = 4;
  p.buffer_frames = 1024;

  if(ioctl(fd, AUDIO_IOC_SET_PARAMS, &p) < 0){
    close(fd);
    return -1;
  }
  if(ioctl(fd, AUDIO_IOC_PREPARE, 0) < 0){
    close(fd);
    return -1;
  }
  return fd;
}

static int
stream_recover(int fd)
{
  if(ioctl(fd, AUDIO_IOC_RESET_XRUN, 0) < 0)
    return -1;
  if(ioctl(fd, AUDIO_IOC_PREPARE, 0) < 0)
    return -1;
  return 0;
}

int
main(int argc, char *argv[])
{
  struct sstate   streams[MAX_STREAMS];
  struct pollfd   pfds[MAX_STREAMS];
  int             pfd_map[MAX_STREAMS]; /* pfds[i] -> streams[] index */
  uint8_t         wbuf[WBUF_MAX];
  int             i;
  int             j;
  int             n;
  int             nfds;
  int             active;
  int             ready;
  int             any_fail;
  int             flags;
  struct sstate  *st;

  for(i = 1; i < argc; i++){
    if(argv[i][0] == '-'){
      switch(argv[i][1]){
      case 'n':
        if(++i >= argc) usage();
        nstreams = atoi(argv[i]);
        break;
      case 'c':
        if(++i >= argc) usage();
        total_chunks = atoi(argv[i]);
        break;
      case 'b':
        if(++i >= argc) usage();
        chunk_bytes = atoi(argv[i]);
        break;
      case 't':
        if(++i >= argc) usage();
        timeout_ms = atoi(argv[i]);
        break;
      case 'N':
        test_nonblock = 1;
        break;
      case 'v':
        verbose = 1;
        break;
      default:
        usage();
      }
    } else {
      devpath = argv[i];
    }
  }

  if(nstreams < 1 || nstreams > MAX_STREAMS)
    usage();
  if(total_chunks <= 0 || chunk_bytes <= 0 || chunk_bytes > WBUF_MAX)
    usage();

  /* Open and configure all streams */
  for(i = 0; i < nstreams; i++){
    memset(&streams[i], 0, sizeof(streams[i]));
    streams[i].fd = stream_open(devpath);
    if(streams[i].fd < 0){
      dprintf(2, "audiopollstress: failed to open/configure stream %d on %s\n",
              i, devpath);
      for(j = 0; j < i; j++)
        close(streams[j].fd);
      exit(1);
    }

    if(test_nonblock){
      flags = fcntl(streams[i].fd, F_GETFL, 0);
      if(flags < 0 || fcntl(streams[i].fd, F_SETFL, flags | O_NONBLOCK) < 0){
        dprintf(2, "audiopollstress: F_SETFL O_NONBLOCK failed on stream %d\n", i);
        for(j = 0; j <= i; j++)
          close(streams[j].fd);
        exit(1);
      }
    }
  }

  dprintf(1,
    "audiopollstress: %d stream(s), %d chunks/stream, %d bytes/chunk,"
    " timeout=%d ms%s\n",
    nstreams, total_chunks, chunk_bytes, timeout_ms,
    test_nonblock ? ", O_NONBLOCK" : "");

  /* Synthetic write buffer — deterministic pattern */
  for(i = 0; i < chunk_bytes; i++)
    wbuf[i] = (uint8_t)(i & 0xff);

  active = nstreams;

  while(active > 0){
    /* Build pollfd array for streams still delivering chunks */
    nfds = 0;
    for(i = 0; i < nstreams; i++){
      if(streams[i].chunks_done >= total_chunks || streams[i].failed)
        continue;
      pfds[nfds].fd      = streams[i].fd;
      pfds[nfds].events  = POLLOUT;
      pfds[nfds].revents = 0;
      pfd_map[nfds]      = i;
      nfds++;
    }

    if(nfds == 0)
      break;

    ready = poll(pfds, (nfds_t)nfds, timeout_ms);
    if(ready < 0){
      dprintf(2, "audiopollstress: poll() failed\n");
      break;
    }
    if(ready == 0){
      if(verbose)
        dprintf(1, "audiopollstress: poll timeout (%d ms, %d active)\n",
                timeout_ms, active);
      continue;
    }

    for(j = 0; j < nfds; j++){
      if(pfds[j].revents == 0)
        continue;

      st = &streams[pfd_map[j]];

      if(pfds[j].revents & POLLERR){
        st->poll_errs++;
        st->xruns++;
        if(verbose)
          dprintf(1, "audiopollstress: stream %d POLLERR xrun recovery\n",
                  pfd_map[j]);
        if(stream_recover(st->fd) < 0){
          dprintf(2, "audiopollstress: stream %d xrun recovery failed\n",
                  pfd_map[j]);
          st->failed = 1;
          active--;
        } else {
          st->recoveries++;
        }
        continue;
      }

      if(pfds[j].revents & POLLOUT){
        st->poll_hits++;
        n = write(st->fd, (char*)wbuf, chunk_bytes);
        if(n < 0){
          st->write_errs++;
          if(verbose)
            dprintf(1,
                    "audiopollstress: stream %d write fail after POLLOUT"
                    " (chunk %d)\n",
                    pfd_map[j], st->chunks_done);
        } else {
          st->writes++;
          st->chunks_done++;
          if(verbose && (st->chunks_done % 16) == 0)
            dprintf(1, "audiopollstress: stream %d  %d/%d chunks\n",
                    pfd_map[j], st->chunks_done, total_chunks);
        }

        if(st->chunks_done >= total_chunks){
          (void)ioctl(st->fd, AUDIO_IOC_DRAIN, 0);
          active--;
        }
      }
    }
  }

  /* Final report */
  dprintf(1, "\n%-6s %-8s %-8s %-8s %-8s %-8s %-8s %-10s\n",
          "stream", "chunks", "pollhit", "pollerr",
          "writes",  "werr",   "xruns",  "result");

  any_fail = 0;
  for(i = 0; i < nstreams; i++){
    st = &streams[i];
    n  = (!st->failed) && (st->chunks_done == total_chunks)
         && (st->write_errs == 0);
    if(!n)
      any_fail = 1;
    dprintf(1, "%-6d %-8d %-8d %-8d %-8d %-8d %-8d %-10s\n",
            i,
            st->chunks_done,
            st->poll_hits,
            st->poll_errs,
            st->writes,
            st->write_errs,
            st->xruns,
            n ? "PASS" : "FAIL");
    close(st->fd);
  }

  dprintf(1, "\nResult: %s\n", any_fail ? "FAIL" : "PASS");
  exit(any_fail ? 1 : 0);
}
