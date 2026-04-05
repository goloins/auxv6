#include "types.h"
#include "param.h"
#include "defs.h"
#include "stat.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "proc.h"
#include "audio.h"
#include "audio_ioctl.h"

#define AUDIO_TICKS_PER_SEC 100U

#define AUDIO_STREAM_MAX 64
#define AUDIO_STREAM_RING_BYTES 4096
#define AUDIO_CTL_MINOR 0

struct audio_core_state {
  struct spinlock lock;
  struct audio_default_route def_route;
  struct audio_stream_params params;
  struct audio_stream_volume vol;
  uint16_t card;
  uint16_t device;
  uint16_t direction;
  uint32_t stream_state;
  uint64_t hw_ptr_frames;
  uint64_t sw_ptr_frames;
  uint64_t queued_frames;
  uint32_t ioctl_calls;
  uint32_t write_calls;
  uint32_t bytes_written;
  uint32_t xruns;
  uint32_t late_wakeups;
  uint32_t period_misses;
  uint32_t recoveries;
};

struct audio_stream {
  int in_use;
  struct file *owner;
  int owner_pid;
  int nonblock;
  uint16_t minor;

  struct audio_stream_params params;
  struct audio_stream_volume vol;
  uint32_t stream_state;

  uint64_t hw_ptr_bytes;
  uint64_t sw_ptr_bytes;
  uint64_t queued_frames;
  uint32_t xruns;
  uint32_t late_wakeups;
  uint32_t period_misses;
  uint32_t recoveries;

  char *ring;
  uint32_t ring_size;
  uint32_t ring_head;
  uint32_t ring_tail;
  uint32_t last_tick;
};

struct audio_hw_device {
  uint16_t vendor_id;
  uint16_t device_id;
  uint16_t card;
  uint16_t device;
  uint16_t direction;
  uint16_t reserved0;
  uint32_t flags;
  uint32_t hw_profile;
};

static struct audio_core_state audio_core;
static struct audio_hw_device audio_hw_devs[AUDIO_MAX_DEVICES];
static uint32_t audio_hw_count;
static struct audio_stream audio_streams[AUDIO_STREAM_MAX];
static uint16_t audio_hw_next_card;

static int
audio_validate_payload(uint32_t abi_version, uint32_t struct_size, uint32_t min_size)
{
  if(abi_version == 0 || struct_size == 0)
    return -1;
  if(((abi_version >> 16) & 0xffU) != (uint32_t)AUDIO_ABI_MAJOR)
    return -1;
  if(struct_size < min_size)
    return -1;
  return 0;
}

static uint32_t
audio_bytes_per_sample(uint32_t fmt)
{
  switch(fmt){
  case AUDIO_FMT_U8:
    return 1;
  case AUDIO_FMT_S16_LE:
    return 2;
  case AUDIO_FMT_S24_LE:
    return 3;
  case AUDIO_FMT_S32_LE:
    return 4;
  default:
    return 0;
  }
}

static int
audio_state_prepare_ok(uint32_t st)
{
  return st == AUDIO_ST_NEW || st == AUDIO_ST_CONFIGURED || st == AUDIO_ST_STOPPED ||
         st == AUDIO_ST_XRUN || st == AUDIO_ST_DRAINED;
}

static int
audio_state_start_ok(uint32_t st)
{
  return st == AUDIO_ST_PREPARED || st == AUDIO_ST_STOPPED || st == AUDIO_ST_CONFIGURED;
}

static int
audio_state_stop_ok(uint32_t st)
{
  return st == AUDIO_ST_RUNNING || st == AUDIO_ST_PREPARED || st == AUDIO_ST_CONFIGURED;
}

static int
audio_state_drain_ok(uint32_t st)
{
  return st == AUDIO_ST_RUNNING || st == AUDIO_ST_STOPPED ||
         st == AUDIO_ST_PREPARED || st == AUDIO_ST_CONFIGURED;
}

static const char*
audio_state_name(uint32_t st)
{
  switch(st){
  case AUDIO_ST_NEW:
    return "new";
  case AUDIO_ST_CONFIGURED:
    return "configured";
  case AUDIO_ST_PREPARED:
    return "prepared";
  case AUDIO_ST_RUNNING:
    return "running";
  case AUDIO_ST_XRUN:
    return "xrun";
  case AUDIO_ST_STOPPED:
    return "stopped";
  case AUDIO_ST_DRAINED:
    return "drained";
  default:
    return "unknown";
  }
}

static int
audio_is_stream_minor(int minor)
{
  return minor != AUDIO_CTL_MINOR;
}

static struct audio_stream*
audio_stream_find_locked(struct file *f)
{
  int i;

  for(i = 0; i < AUDIO_STREAM_MAX; i++){
    if(!audio_streams[i].in_use)
      continue;
    if(audio_streams[i].owner == f)
      return &audio_streams[i];
  }
  return 0;
}

static struct audio_stream*
audio_stream_alloc_locked(struct file *f, int minor, int nonblock, char *ring)
{
  int i;
  struct audio_stream *s;
  struct proc *p;

  for(i = 0; i < AUDIO_STREAM_MAX; i++){
    if(audio_streams[i].in_use)
      continue;

    s = &audio_streams[i];
    memset(s, 0, sizeof(*s));
    p = myproc();
    s->in_use = 1;
    s->owner = f;
    s->owner_pid = p ? p->pid : -1;
    s->nonblock = nonblock;
    s->minor = (uint16_t)minor;
    s->ring = ring;
    s->ring_size = AUDIO_STREAM_RING_BYTES;

    s->params.abi_version = AUDIO_ABI_VERSION;
    s->params.struct_size = sizeof(s->params);
    s->params.sample_rate = AUDIO_DEFAULT_RATE_HZ;
    s->params.channels = AUDIO_DEFAULT_CHANNELS;
    s->params.sample_format = AUDIO_FMT_S16_LE;
    s->params.period_frames = AUDIO_DEFAULT_PERIOD_FRAMES;
    s->params.periods = AUDIO_DEFAULT_PERIODS;
    s->params.buffer_frames = AUDIO_DEFAULT_BUFFER_FRAMES;

    s->vol.abi_version = AUDIO_ABI_VERSION;
    s->vol.struct_size = sizeof(s->vol);
    s->vol.left_db_q8_8 = AUDIO_VOL_MAX_DB_Q8_8;
    s->vol.right_db_q8_8 = AUDIO_VOL_MAX_DB_Q8_8;

    s->stream_state = AUDIO_ST_NEW;
    acquire(&tickslock);
    s->last_tick = ticks;
    release(&tickslock);
    return s;
  }
  return 0;
}

static uint32_t
audio_stream_frame_bytes(const struct audio_stream *s)
{
  uint32_t bps;

  if(s == 0)
    return 0;
  bps = audio_bytes_per_sample(s->params.sample_format);
  if(bps == 0 || s->params.channels == 0)
    return 0;
  return bps * s->params.channels;
}

static uint32_t
audio_stream_used_bytes(const struct audio_stream *s)
{
  if(s->ring_tail >= s->ring_head)
    return s->ring_tail - s->ring_head;
  return s->ring_size - s->ring_head + s->ring_tail;
}

static uint32_t
audio_stream_free_bytes(const struct audio_stream *s)
{
  return s->ring_size - audio_stream_used_bytes(s);
}

static void
audio_stream_update_queue_frames(struct audio_stream *s)
{
  uint32_t frame_bytes;
  uint32_t used;

  frame_bytes = audio_stream_frame_bytes(s);
  if(frame_bytes == 0){
    s->queued_frames = 0;
    return;
  }

  used = audio_stream_used_bytes(s);
  s->queued_frames = used / frame_bytes;
}

static void
audio_stream_consume_locked(struct audio_stream *s)
{
  uint32_t now;
  uint32_t elapsed;
  uint32_t frame_bytes;
  uint64_t budget_frames;
  uint64_t budget_bytes;
  uint32_t used;
  uint32_t consume;

  acquire(&tickslock);
  now = ticks;
  release(&tickslock);

  if(now == s->last_tick)
    return;

  elapsed = now - s->last_tick;
  s->last_tick = now;

  if(s->stream_state != AUDIO_ST_RUNNING)
    return;

  frame_bytes = audio_stream_frame_bytes(s);
  if(frame_bytes == 0)
    return;

  budget_frames = ((uint64_t)elapsed * (uint64_t)s->params.sample_rate) /
                  AUDIO_TICKS_PER_SEC;
  if(budget_frames == 0)
    return;
  budget_bytes = budget_frames * frame_bytes;

  used = audio_stream_used_bytes(s);
  if(used == 0){
    s->period_misses += elapsed;
    return;
  }

  consume = (uint32_t)budget_bytes;
  if(consume > used)
    consume = used;

  s->ring_head = (s->ring_head + consume) % s->ring_size;
  s->hw_ptr_bytes += consume;
  audio_stream_update_queue_frames(s);
  wakeup(&s->ring_head);
}

int
audio_open(struct file *f, int minor, int omode)
{
  struct audio_stream *s;
  char *ring;

  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return -1;
  if(f->ip->type != T_DEV || f->ip->major != AUDIODEV)
    return -1;
  if(!audio_is_stream_minor(minor))
    return 0;

  ring = kalloc();
  if(ring == 0)
    return -1;

  acquire(&audio_core.lock);
  s = audio_stream_find_locked(f);
  if(s != 0){
    release(&audio_core.lock);
    kfree(ring);
    return 0;
  }

  s = audio_stream_alloc_locked(f, minor, (omode & O_NONBLOCK) != 0, ring);
  if(s == 0){
    release(&audio_core.lock);
    kfree(ring);
    return -1;
  }
  release(&audio_core.lock);
  return 0;
}

void
audio_close(struct file *f)
{
  int i;
  char *ring;

  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return;
  if(f->ip->type != T_DEV || f->ip->major != AUDIODEV)
    return;
  if(!audio_is_stream_minor(f->ip->minor))
    return;

  ring = 0;
  acquire(&audio_core.lock);
  for(i = 0; i < AUDIO_STREAM_MAX; i++){
    if(!audio_streams[i].in_use)
      continue;
    if(audio_streams[i].owner != f)
      continue;

    ring = audio_streams[i].ring;
    wakeup(&audio_streams[i].ring_head);
    memset(&audio_streams[i], 0, sizeof(audio_streams[i]));
    break;
  }
  release(&audio_core.lock);

  if(ring)
    kfree(ring);
}

static int
audio_buf_putc(char *buf, int max, int *len, char c)
{
  if(*len >= max)
    return -1;
  buf[*len] = c;
  (*len)++;
  return 0;
}

static int
audio_buf_puts(char *buf, int max, int *len, const char *s)
{
  int i;

  for(i = 0; s[i]; i++){
    if(audio_buf_putc(buf, max, len, s[i]) < 0)
      return -1;
  }
  return 0;
}

static int
audio_buf_putu(char *buf, int max, int *len, uint v)
{
  char tmp[16];
  int n;
  int i;

  n = 0;
  do {
    tmp[n++] = (char)('0' + (v % 10));
    v /= 10;
  } while(v > 0 && n < (int)sizeof(tmp));

  for(i = n - 1; i >= 0; i--){
    if(audio_buf_putc(buf, max, len, tmp[i]) < 0)
      return -1;
  }
  return 0;
}

static int
audio_buf_puts32(char *buf, int max, int *len, int v)
{
  uint uv;

  if(v < 0){
    if(audio_buf_putc(buf, max, len, '-') < 0)
      return -1;
    uv = (uint)(-v);
  } else {
    uv = (uint)v;
  }
  return audio_buf_putu(buf, max, len, uv);
}

static void
audio_fill_abi(struct audio_abi_info *abi)
{
  if(abi == 0)
    return;

  abi->abi_version = AUDIO_ABI_VERSION;
  abi->struct_size = sizeof(*abi);
  abi->abi_major = AUDIO_ABI_MAJOR;
  abi->abi_minor = AUDIO_ABI_MINOR;
  abi->abi_patch = AUDIO_ABI_PATCH;
  abi->reserved0 = 0;
  memset(abi->reserved1, 0, sizeof(abi->reserved1));
}

static int
audio_param_valid(const struct audio_stream_params *p)
{
  if(p == 0)
    return 0;
  if(p->sample_rate == 0 || p->channels == 0)
    return 0;
  if(p->sample_format > AUDIO_FMT_U8)
    return 0;
  if(p->period_frames == 0 || p->periods == 0)
    return 0;
  return 1;
}

static struct audio_hw_device*
audio_find_hw_device(uint16_t card, uint16_t device, uint16_t direction)
{
  uint32_t i;

  for(i = 0; i < audio_hw_count; i++){
    if(audio_hw_devs[i].card != card)
      continue;
    if(audio_hw_devs[i].device != device)
      continue;
    if(audio_hw_devs[i].direction != direction)
      continue;
    return &audio_hw_devs[i];
  }
  return 0;
}

static struct audio_hw_device*
audio_pick_default_hw_device(void)
{
  struct audio_hw_device *dev;

  dev = audio_find_hw_device(audio_core.def_route.card,
                             audio_core.def_route.device,
                             audio_core.def_route.direction);
  if(dev)
    return dev;
  if(audio_hw_count == 0)
    return 0;
  return &audio_hw_devs[0];
}

static void
audio_fill_caps_for_profile(struct audio_hw_caps *caps, uint32_t profile)
{
  caps->min_channels = 1;
  caps->max_channels = 2;
  caps->format_count = 2;
  caps->formats[0] = AUDIO_FMT_S16_LE;
  caps->formats[1] = AUDIO_FMT_U8;
  caps->rate_count = 1;
  caps->rates[0] = AUDIO_DEFAULT_RATE_HZ;
  caps->min_period_frames = AUDIO_DEFAULT_PERIOD_FRAMES;
  caps->max_period_frames = AUDIO_DEFAULT_PERIOD_FRAMES;
  caps->min_periods = AUDIO_DEFAULT_PERIODS;
  caps->max_periods = AUDIO_DEFAULT_PERIODS;

  switch(profile){
  case AUDIO_HW_PROFILE_HDA:
    caps->min_rate = 48000;
    caps->max_rate = 192000;
    caps->format_count = 3;
    caps->formats[0] = AUDIO_FMT_S16_LE;
    caps->formats[1] = AUDIO_FMT_S24_LE;
    caps->formats[2] = AUDIO_FMT_S32_LE;
    caps->rate_count = 3;
    caps->rates[0] = 48000;
    caps->rates[1] = 96000;
    caps->rates[2] = 192000;
    break;
  case AUDIO_HW_PROFILE_AC97:
    caps->min_rate = 48000;
    caps->max_rate = 48000;
    break;
  case AUDIO_HW_PROFILE_LEGACY_PCI_PCM:
    caps->min_rate = 44100;
    caps->max_rate = 48000;
    caps->rate_count = 2;
    caps->rates[0] = 44100;
    caps->rates[1] = 48000;
    break;
  default:
    caps->min_rate = AUDIO_DEFAULT_RATE_HZ;
    caps->max_rate = AUDIO_DEFAULT_RATE_HZ;
    break;
  }
}

static int
audioread(struct inode *ip, char *dst, uint64_t off, int n)
{
  (void)ip;
  (void)dst;
  (void)off;
  (void)n;
  return -1;
}

static int
audiowrite(struct inode *ip, char *src, uint64_t off, int n)
{
  (void)ip;
  (void)src;
  (void)off;
  /* Stage-1 writes should flow through audio_filewrite(), which has fd context. */
  return -1;
}

int
audio_filewrite(struct file *f, char *src, int n)
{
  struct audio_stream *s;
  uint32_t frame_bytes;
  int copied;
  int chunk;
  int left;

  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return -1;
  if(f->ip->type != T_DEV || f->ip->major != AUDIODEV)
    return -1;
  if(!audio_is_stream_minor(f->ip->minor))
    return -1;
  if(n < 0)
    return -1;

  acquire(&audio_core.lock);
  s = audio_stream_find_locked(f);
  if(s == 0){
    release(&audio_core.lock);
    if(audio_open(f, f->ip->minor, 0) < 0)
      return -1;
    acquire(&audio_core.lock);
    s = audio_stream_find_locked(f);
    if(s == 0){
      release(&audio_core.lock);
      return -1;
    }
  }

  frame_bytes = audio_stream_frame_bytes(s);
  if(frame_bytes == 0){
    release(&audio_core.lock);
    return -1;
  }

  if(s->stream_state == AUDIO_ST_NEW)
    s->stream_state = AUDIO_ST_CONFIGURED;
  if(s->stream_state == AUDIO_ST_CONFIGURED ||
     s->stream_state == AUDIO_ST_PREPARED ||
     s->stream_state == AUDIO_ST_STOPPED)
    s->stream_state = AUDIO_ST_RUNNING;

  copied = 0;
  left = n;
  while(left > 0){
    audio_stream_consume_locked(s);

    if(audio_stream_free_bytes(s) == 0){
      if(s->nonblock){
        if(copied == 0){
          release(&audio_core.lock);
          return -1;
        }
        break;
      }

      sleep(&s->ring_head, &audio_core.lock);
      if(!s->in_use || s->owner != f){
        release(&audio_core.lock);
        return (copied > 0) ? copied : -1;
      }
      continue;
    }

    chunk = (int)audio_stream_free_bytes(s);
    if(chunk > left)
      chunk = left;
    if(chunk > (int)(s->ring_size - s->ring_tail))
      chunk = (int)(s->ring_size - s->ring_tail);

    memmove(&s->ring[s->ring_tail], &src[copied], (uint)chunk);
    s->ring_tail = (s->ring_tail + (uint32_t)chunk) % s->ring_size;
    s->sw_ptr_bytes += (uint64_t)chunk;
    audio_stream_update_queue_frames(s);

    copied += chunk;
    left -= chunk;
  }

  audio_core.write_calls++;
  audio_core.bytes_written += (uint32_t)copied;
  audio_core.stream_state = s->stream_state;
  audio_core.hw_ptr_frames = s->hw_ptr_bytes / frame_bytes;
  audio_core.sw_ptr_frames = s->sw_ptr_bytes / frame_bytes;
  audio_core.queued_frames = s->queued_frames;
  audio_core.xruns = s->xruns;
  audio_core.late_wakeups = s->late_wakeups;
  audio_core.period_misses = s->period_misses;
  audio_core.recoveries = s->recoveries;

  release(&audio_core.lock);
  return copied;
}

int
audio_set_nonblock(struct file *f, int enabled)
{
  struct audio_stream *s;

  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return -1;
  if(f->ip->type != T_DEV || f->ip->major != AUDIODEV)
    return -1;
  if(!audio_is_stream_minor(f->ip->minor))
    return -1;

  acquire(&audio_core.lock);
  s = audio_stream_find_locked(f);
  if(s == 0){
    release(&audio_core.lock);
    return -1;
  }
  s->nonblock = enabled ? 1 : 0;
  release(&audio_core.lock);
  return 0;
}

int
audio_get_nonblock(struct file *f)
{
  struct audio_stream *s;
  int nonblock;

  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return -1;
  if(f->ip->type != T_DEV || f->ip->major != AUDIODEV)
    return -1;
  if(!audio_is_stream_minor(f->ip->minor))
    return -1;

  acquire(&audio_core.lock);
  s = audio_stream_find_locked(f);
  if(s == 0){
    release(&audio_core.lock);
    return -1;
  }
  nonblock = s->nonblock;
  release(&audio_core.lock);
  return nonblock;
}

void
audio_poll_events(struct file *f, int *rd, int *wr, int *err)
{
  struct audio_stream *s;

  if(rd)
    *rd = 0;
  if(wr)
    *wr = 0;
  if(err)
    *err = 0;

  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return;
  if(f->ip->type != T_DEV || f->ip->major != AUDIODEV)
    return;

  if(!audio_is_stream_minor(f->ip->minor)){
    if(rd && f->readable)
      *rd = 1;
    if(wr && f->writable)
      *wr = 1;
    return;
  }

  acquire(&audio_core.lock);
  s = audio_stream_find_locked(f);
  if(s == 0){
    release(&audio_core.lock);
    if(err)
      *err = 1;
    return;
  }

  audio_stream_consume_locked(s);
  if(err && s->stream_state == AUDIO_ST_XRUN)
    *err = 1;

  if(wr && f->writable && audio_stream_free_bytes(s) > 0)
    *wr = 1;

  if(rd && f->readable)
    *rd = 0;

  release(&audio_core.lock);
}

int
audio_is_ioctl(int request)
{
  switch(request){
  case AUDIO_IOC_QUERY_ABI:
  case AUDIO_IOC_ENUM_DEVICES:
  case AUDIO_IOC_QUERY_CAPS:
  case AUDIO_IOC_SET_DEFAULT:
  case AUDIO_IOC_GET_DEFAULT:
  case AUDIO_IOC_SET_PARAMS:
  case AUDIO_IOC_GET_PARAMS:
  case AUDIO_IOC_PREPARE:
  case AUDIO_IOC_START:
  case AUDIO_IOC_STOP:
  case AUDIO_IOC_DRAIN:
  case AUDIO_IOC_DROP:
  case AUDIO_IOC_GET_STATUS:
  case AUDIO_IOC_SET_STREAM_VOL:
  case AUDIO_IOC_GET_STREAM_VOL:
  case AUDIO_IOC_RESET_XRUN:
    return 1;
  default:
    return 0;
  }
}

int
audio_ioctl_arg_size(int request)
{
  switch(request){
  case AUDIO_IOC_QUERY_ABI:
    return sizeof(struct audio_abi_info);
  case AUDIO_IOC_ENUM_DEVICES:
    return sizeof(struct audio_enum_devices);
  case AUDIO_IOC_QUERY_CAPS:
    return sizeof(struct audio_hw_caps);
  case AUDIO_IOC_SET_DEFAULT:
  case AUDIO_IOC_GET_DEFAULT:
    return sizeof(struct audio_default_route);
  case AUDIO_IOC_SET_PARAMS:
  case AUDIO_IOC_GET_PARAMS:
    return sizeof(struct audio_stream_params);
  case AUDIO_IOC_PREPARE:
  case AUDIO_IOC_START:
  case AUDIO_IOC_STOP:
  case AUDIO_IOC_DRAIN:
  case AUDIO_IOC_DROP:
  case AUDIO_IOC_RESET_XRUN:
    return sizeof(struct audio_cmd);
  case AUDIO_IOC_GET_STATUS:
    return sizeof(struct audio_stream_status);
  case AUDIO_IOC_SET_STREAM_VOL:
  case AUDIO_IOC_GET_STREAM_VOL:
    return sizeof(struct audio_stream_volume);
  default:
    return -1;
  }
}

int
audio_ioctl_file(struct file *f, int request, uint arg)
{
  struct audio_abi_info *abi;
  struct audio_enum_devices *edevs;
  struct audio_hw_caps *caps;
  struct audio_default_route *route;
  struct audio_stream_params *params;
  struct audio_stream_status *st;
  struct audio_stream_volume *vol;
  struct audio_hw_device *hw;
  struct audio_stream *s;
  uint32_t i;
  uint32_t limit;
  uint32_t frame_bytes;
  int is_stream_cmd;

  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return -1;
  if(f->ip->type != T_DEV || f->ip->major != AUDIODEV)
    return -1;

  is_stream_cmd = 0;
  switch(request){
  case AUDIO_IOC_SET_PARAMS:
  case AUDIO_IOC_GET_PARAMS:
  case AUDIO_IOC_PREPARE:
  case AUDIO_IOC_START:
  case AUDIO_IOC_STOP:
  case AUDIO_IOC_DRAIN:
  case AUDIO_IOC_DROP:
  case AUDIO_IOC_GET_STATUS:
  case AUDIO_IOC_SET_STREAM_VOL:
  case AUDIO_IOC_GET_STREAM_VOL:
  case AUDIO_IOC_RESET_XRUN:
    is_stream_cmd = 1;
    break;
  default:
    break;
  }

  if(is_stream_cmd && !audio_is_stream_minor(f->ip->minor))
    return -1;

  s = 0;
  if(is_stream_cmd){
    acquire(&audio_core.lock);
    s = audio_stream_find_locked(f);
    release(&audio_core.lock);
    if(s == 0){
      if(audio_open(f, f->ip->minor, 0) < 0)
        return -1;
      acquire(&audio_core.lock);
      s = audio_stream_find_locked(f);
      release(&audio_core.lock);
      if(s == 0)
        return -1;
    }
  }

  acquire(&audio_core.lock);
  audio_core.ioctl_calls++;
  release(&audio_core.lock);

  switch(request){
  case AUDIO_IOC_QUERY_ABI:
    abi = (struct audio_abi_info*)arg;
    if(abi == 0 || audio_validate_payload(abi->abi_version, abi->struct_size, sizeof(*abi)) < 0)
      return -1;
    audio_fill_abi(abi);
    return 0;

  case AUDIO_IOC_ENUM_DEVICES:
    edevs = (struct audio_enum_devices*)arg;
    if(edevs == 0)
      return -1;
    if(audio_validate_payload(edevs->abi_version, edevs->struct_size, sizeof(*edevs)) < 0)
      return -1;
    edevs->abi_version = AUDIO_ABI_VERSION;
    edevs->struct_size = sizeof(*edevs);
    acquire(&audio_core.lock);
    edevs->num_entries = audio_hw_count;
    limit = edevs->max_entries;
    if(limit > audio_hw_count)
      limit = audio_hw_count;
    if(edevs->entries_ptr != 0 && limit > 0){
      struct audio_device_info *list;
      list = (struct audio_device_info*)(uint)edevs->entries_ptr;
      for(i = 0; i < limit; i++){
        list[i].abi_version = AUDIO_ABI_VERSION;
        list[i].struct_size = sizeof(*list);
        list[i].card = audio_hw_devs[i].card;
        list[i].device = audio_hw_devs[i].device;
        list[i].direction = audio_hw_devs[i].direction;
        list[i].reserved0 = 0;
        list[i].flags = audio_hw_devs[i].flags;
        memset(list[i].reserved1, 0, sizeof(list[i].reserved1));
      }
    }
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_QUERY_CAPS:
    caps = (struct audio_hw_caps*)arg;
    if(caps == 0)
      return -1;
    if(audio_validate_payload(caps->abi_version, caps->struct_size, sizeof(*caps)) < 0)
      return -1;
    acquire(&audio_core.lock);
    hw = audio_find_hw_device(caps->card, caps->device, caps->direction);
    if(hw == 0)
      hw = audio_pick_default_hw_device();
    if(hw == 0){
      release(&audio_core.lock);
      return -1;
    }
    caps->abi_version = AUDIO_ABI_VERSION;
    caps->struct_size = sizeof(*caps);
    caps->card = hw->card;
    caps->device = hw->device;
    caps->direction = hw->direction;
    memset(caps->formats, 0, sizeof(caps->formats));
    memset(caps->rates, 0, sizeof(caps->rates));
    audio_fill_caps_for_profile(caps, hw->hw_profile);
    caps->flags = hw->flags;
    memset(caps->reserved1, 0, sizeof(caps->reserved1));
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_SET_DEFAULT:
    route = (struct audio_default_route*)arg;
    if(route == 0)
      return -1;
    if(audio_validate_payload(route->abi_version, route->struct_size, sizeof(*route)) < 0)
      return -1;
    acquire(&audio_core.lock);
    audio_core.def_route.card = route->card;
    audio_core.def_route.device = route->device;
    audio_core.def_route.direction = route->direction;
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_GET_DEFAULT:
    route = (struct audio_default_route*)arg;
    if(route == 0)
      return -1;
    if(audio_validate_payload(route->abi_version, route->struct_size, sizeof(*route)) < 0)
      return -1;
    acquire(&audio_core.lock);
    *route = audio_core.def_route;
    route->abi_version = AUDIO_ABI_VERSION;
    route->struct_size = sizeof(*route);
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_SET_PARAMS:
    params = (struct audio_stream_params*)arg;
    if(params == 0)
      return -1;
    if(audio_validate_payload(params->abi_version, params->struct_size, sizeof(*params)) < 0)
      return -1;
    if(!audio_param_valid(params))
      return -1;
    acquire(&audio_core.lock);
    s->params = *params;
    if(s->params.buffer_frames == 0)
      s->params.buffer_frames = s->params.period_frames * s->params.periods;
    s->params.abi_version = AUDIO_ABI_VERSION;
    s->params.struct_size = sizeof(s->params);
    s->stream_state = AUDIO_ST_CONFIGURED;
    s->ring_head = 0;
    s->ring_tail = 0;
    s->hw_ptr_bytes = 0;
    s->sw_ptr_bytes = 0;
    s->queued_frames = 0;
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_GET_PARAMS:
    params = (struct audio_stream_params*)arg;
    if(params == 0)
      return -1;
    if(audio_validate_payload(params->abi_version, params->struct_size, sizeof(*params)) < 0)
      return -1;
    acquire(&audio_core.lock);
    *params = s->params;
    params->abi_version = AUDIO_ABI_VERSION;
    params->struct_size = sizeof(*params);
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_PREPARE:
    acquire(&audio_core.lock);
    if(!audio_state_prepare_ok(s->stream_state)){
      release(&audio_core.lock);
      return -1;
    }
    if(s->stream_state == AUDIO_ST_XRUN)
      s->recoveries++;
    s->stream_state = AUDIO_ST_PREPARED;
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_START:
    acquire(&audio_core.lock);
    if(!audio_state_start_ok(s->stream_state)){
      release(&audio_core.lock);
      return -1;
    }
    s->stream_state = AUDIO_ST_RUNNING;
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_STOP:
    acquire(&audio_core.lock);
    if(!audio_state_stop_ok(s->stream_state)){
      release(&audio_core.lock);
      return -1;
    }
    s->stream_state = AUDIO_ST_STOPPED;
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_DRAIN:
    acquire(&audio_core.lock);
    if(!audio_state_drain_ok(s->stream_state)){
      release(&audio_core.lock);
      return -1;
    }
    while(audio_stream_used_bytes(s) > 0){
      audio_stream_consume_locked(s);
      if(audio_stream_used_bytes(s) > 0){
        if(s->nonblock){
          release(&audio_core.lock);
          return -1;
        }
        sleep(&s->ring_head, &audio_core.lock);
        if(!s->in_use || s->owner != f){
          release(&audio_core.lock);
          return -1;
        }
      }
    }
    s->stream_state = AUDIO_ST_DRAINED;
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_DROP:
    acquire(&audio_core.lock);
    s->ring_head = 0;
    s->ring_tail = 0;
    s->queued_frames = 0;
    s->stream_state = AUDIO_ST_STOPPED;
    wakeup(&s->ring_head);
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_GET_STATUS:
    st = (struct audio_stream_status*)arg;
    if(st == 0)
      return -1;
    if(audio_validate_payload(st->abi_version, st->struct_size, sizeof(*st)) < 0)
      return -1;
    memset(st, 0, sizeof(*st));
    acquire(&audio_core.lock);
    audio_stream_consume_locked(s);
    frame_bytes = audio_stream_frame_bytes(s);
    st->state = s->stream_state;
    if(frame_bytes == 0)
      frame_bytes = 1;
    st->hw_ptr_frames = s->hw_ptr_bytes / frame_bytes;
    st->sw_ptr_frames = s->sw_ptr_bytes / frame_bytes;
    st->queued_frames = s->queued_frames;
    st->delay_frames = s->queued_frames;
    st->xruns = s->xruns;
    st->late_wakeups = s->late_wakeups;
    st->period_misses = s->period_misses;
    st->recoveries = s->recoveries;
    release(&audio_core.lock);
    st->abi_version = AUDIO_ABI_VERSION;
    st->struct_size = sizeof(*st);
    return 0;

  case AUDIO_IOC_SET_STREAM_VOL:
    vol = (struct audio_stream_volume*)arg;
    if(vol == 0)
      return -1;
    if(audio_validate_payload(vol->abi_version, vol->struct_size, sizeof(*vol)) < 0)
      return -1;
    acquire(&audio_core.lock);
    s->vol = *vol;
    s->vol.abi_version = AUDIO_ABI_VERSION;
    s->vol.struct_size = sizeof(s->vol);
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_GET_STREAM_VOL:
    vol = (struct audio_stream_volume*)arg;
    if(vol == 0)
      return -1;
    if(audio_validate_payload(vol->abi_version, vol->struct_size, sizeof(*vol)) < 0)
      return -1;
    acquire(&audio_core.lock);
    *vol = s->vol;
    vol->abi_version = AUDIO_ABI_VERSION;
    vol->struct_size = sizeof(*vol);
    release(&audio_core.lock);
    return 0;

  case AUDIO_IOC_RESET_XRUN:
    acquire(&audio_core.lock);
    if(s->stream_state != AUDIO_ST_XRUN){
      release(&audio_core.lock);
      return -1;
    }
    s->ring_head = 0;
    s->ring_tail = 0;
    s->queued_frames = 0;
    s->hw_ptr_bytes = s->sw_ptr_bytes;
    s->stream_state = AUDIO_ST_PREPARED;
    s->recoveries++;
    wakeup(&s->ring_head);
    release(&audio_core.lock);
    return 0;

  default:
    return -1;
  }
}

int
audio_procfs_summary(char *buf, int max)
{
  int len;
  int i;
  struct audio_default_route def_route;
  struct audio_stream_params params;
  struct audio_stream_volume vol;
  uint state;
  uint hw_count;
  uint active_streams;

  if(buf == 0 || max <= 0)
    return -1;

  acquire(&audio_core.lock);
  def_route = audio_core.def_route;
  params = audio_core.params;
  vol = audio_core.vol;
  state = audio_core.stream_state;
  hw_count = audio_hw_count;
  active_streams = 0;
  for(i = 0; i < AUDIO_STREAM_MAX; i++){
    if(audio_streams[i].in_use)
      active_streams++;
  }
  release(&audio_core.lock);

  len = 0;
  if(audio_buf_puts(buf, max, &len, "abi ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, (uint)AUDIO_ABI_MAJOR) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '.') < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, (uint)AUDIO_ABI_MINOR) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;

  if(audio_buf_puts(buf, max, &len, "state ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, state) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;

  if(audio_buf_puts(buf, max, &len, "devices ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, hw_count) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;

  if(audio_buf_puts(buf, max, &len, "streams active ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, active_streams) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;

  if(audio_buf_puts(buf, max, &len, "default card ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, def_route.card) < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, " device ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, def_route.device) < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, " dir ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, def_route.direction) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;

  if(audio_buf_puts(buf, max, &len, "params rate ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, params.sample_rate) < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, " channels ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, params.channels) < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, " format ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, params.sample_format) < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, " period ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, params.period_frames) < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, " periods ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, params.periods) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;

  if(audio_buf_puts(buf, max, &len, "queue frames ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, (uint)audio_core.queued_frames) < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, " hw_ptr ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, (uint)audio_core.hw_ptr_frames) < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, " sw_ptr ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, (uint)audio_core.sw_ptr_frames) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;

  if(audio_buf_puts(buf, max, &len, "volume left_db_q8_8 ") < 0)
    return -1;
  if(audio_buf_puts32(buf, max, &len, vol.left_db_q8_8) < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, " right_db_q8_8 ") < 0)
    return -1;
  if(audio_buf_puts32(buf, max, &len, vol.right_db_q8_8) < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, " mute ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, vol.mute) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;

  return len;
}

int
audio_procfs_stats(char *buf, int max)
{
  int len;
  uint ioctl_calls;
  uint write_calls;
  uint bytes_written;
  uint xruns;
  uint late_wakeups;
  uint period_misses;
  uint recoveries;

  if(buf == 0 || max <= 0)
    return -1;

  acquire(&audio_core.lock);
  ioctl_calls = audio_core.ioctl_calls;
  write_calls = audio_core.write_calls;
  bytes_written = audio_core.bytes_written;
  xruns = audio_core.xruns;
  late_wakeups = audio_core.late_wakeups;
  period_misses = audio_core.period_misses;
  recoveries = audio_core.recoveries;
  release(&audio_core.lock);

  len = 0;
  if(audio_buf_puts(buf, max, &len, "ioctl_calls ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, ioctl_calls) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, "write_calls ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, write_calls) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, "bytes_written ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, bytes_written) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, "xruns ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, xruns) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, "late_wakeups ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, late_wakeups) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, "period_misses ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, period_misses) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;
  if(audio_buf_puts(buf, max, &len, "recoveries ") < 0)
    return -1;
  if(audio_buf_putu(buf, max, &len, recoveries) < 0)
    return -1;
  if(audio_buf_putc(buf, max, &len, '\n') < 0)
    return -1;

  return len;
}

int
audio_procfs_clients(char *buf, int max)
{
  int len;
  int i;
  uint32_t used;
  uint32_t freeb;
  uint32_t frame_bytes;
  struct audio_stream *s;

  if(buf == 0 || max <= 0)
    return -1;

  len = 0;
  if(audio_buf_puts(buf, max, &len,
                    "slot pid minor state nonblock used free queued hw sw xruns\n") < 0)
    return -1;

  acquire(&audio_core.lock);
  for(i = 0; i < AUDIO_STREAM_MAX; i++){
    if(!audio_streams[i].in_use)
      continue;

    s = &audio_streams[i];
    audio_stream_consume_locked(s);
    used = audio_stream_used_bytes(s);
    freeb = audio_stream_free_bytes(s);
    frame_bytes = audio_stream_frame_bytes(s);
    if(frame_bytes == 0)
      frame_bytes = 1;

    if(audio_buf_putu(buf, max, &len, (uint)i) < 0)
      goto overflow;
    if(audio_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(audio_buf_putu(buf, max, &len, (uint)s->owner_pid) < 0)
      goto overflow;
    if(audio_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(audio_buf_putu(buf, max, &len, s->minor) < 0)
      goto overflow;
    if(audio_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(audio_buf_puts(buf, max, &len, audio_state_name(s->stream_state)) < 0)
      goto overflow;
    if(audio_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(audio_buf_putu(buf, max, &len, (uint)(s->nonblock != 0)) < 0)
      goto overflow;
    if(audio_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(audio_buf_putu(buf, max, &len, used) < 0)
      goto overflow;
    if(audio_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(audio_buf_putu(buf, max, &len, freeb) < 0)
      goto overflow;
    if(audio_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(audio_buf_putu(buf, max, &len, (uint)s->queued_frames) < 0)
      goto overflow;
    if(audio_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(audio_buf_putu(buf, max, &len, (uint)(s->hw_ptr_bytes / frame_bytes)) < 0)
      goto overflow;
    if(audio_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(audio_buf_putu(buf, max, &len, (uint)(s->sw_ptr_bytes / frame_bytes)) < 0)
      goto overflow;
    if(audio_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(audio_buf_putu(buf, max, &len, s->xruns) < 0)
      goto overflow;
    if(audio_buf_putc(buf, max, &len, '\n') < 0)
      goto overflow;
  }
  release(&audio_core.lock);

  return len;

overflow:
  release(&audio_core.lock);
  return -1;
}

/*
 * Called from the timer interrupt handler on every tick (CPU 0 only).
 * Drives software ring consumption for all running streams and wakes
 * any process blocked in a synchronous write waiting for ring space.
 * This is the software analogue of the hardware DMA completion IRQ.
 */
void
audio_tick(void)
{
  int i;
  struct audio_stream *s;

  acquire(&audio_core.lock);
  for(i = 0; i < AUDIO_STREAM_MAX; i++){
    s = &audio_streams[i];
    if(!s->in_use || s->stream_state != AUDIO_ST_RUNNING)
      continue;
    audio_stream_consume_locked(s);
  }
  release(&audio_core.lock);
}

void
audio_init(void)
{
  initlock(&audio_core.lock, "audio");

  memset(&audio_core.def_route, 0, sizeof(audio_core.def_route));
  audio_core.def_route.abi_version = AUDIO_ABI_VERSION;
  audio_core.def_route.struct_size = sizeof(audio_core.def_route);
  audio_core.def_route.direction = AUDIO_DIR_PLAYBACK;

  memset(&audio_core.params, 0, sizeof(audio_core.params));
  audio_core.params.abi_version = AUDIO_ABI_VERSION;
  audio_core.params.struct_size = sizeof(audio_core.params);
  audio_core.params.sample_rate = AUDIO_DEFAULT_RATE_HZ;
  audio_core.params.channels = AUDIO_DEFAULT_CHANNELS;
  audio_core.params.sample_format = AUDIO_FMT_S16_LE;
  audio_core.params.period_frames = AUDIO_DEFAULT_PERIOD_FRAMES;
  audio_core.params.periods = AUDIO_DEFAULT_PERIODS;
  audio_core.params.buffer_frames = AUDIO_DEFAULT_BUFFER_FRAMES;

  memset(&audio_core.vol, 0, sizeof(audio_core.vol));
  audio_core.vol.abi_version = AUDIO_ABI_VERSION;
  audio_core.vol.struct_size = sizeof(audio_core.vol);
  audio_core.vol.left_db_q8_8 = AUDIO_VOL_MAX_DB_Q8_8;
  audio_core.vol.right_db_q8_8 = AUDIO_VOL_MAX_DB_Q8_8;

  audio_core.stream_state = AUDIO_ST_NEW;
  audio_core.card = 0;
  audio_core.device = 0;
  audio_core.direction = AUDIO_DIR_PLAYBACK;
  audio_core.hw_ptr_frames = 0;
  audio_core.sw_ptr_frames = 0;
  audio_core.queued_frames = 0;

  audio_hw_count = 0;
  audio_hw_next_card = 1;
  audio_register_hw_device(0, 0,
                           0, 0,
                           AUDIO_DIR_PLAYBACK,
                           AUDIO_DEVF_CAN_PLAYBACK |
                           AUDIO_DEVF_OSS_DSP_COMPAT |
                           AUDIO_DEVF_OSS_MIXER_COMPAT,
                           AUDIO_HW_PROFILE_NULL,
                           "audio-null");

  audio_pci_probe_init();

  devsw[AUDIODEV].read = audioread;
  devsw[AUDIODEV].write = audiowrite;

  BOOTDBG("audio: initialized Stage-0 core skeleton (major=%d)\n", AUDIODEV);
}

int
audio_register_hw_device(uint16_t vendor_id, uint16_t device_id,
                         uint16_t card, uint16_t device,
                         uint16_t direction, uint32_t flags,
                         uint32_t hw_profile,
                         const char *driver_name)
{
  struct audio_hw_device *slot;

  if(direction != AUDIO_DIR_PLAYBACK && direction != AUDIO_DIR_CAPTURE)
    return -1;

  acquire(&audio_core.lock);
  if(card == AUDIO_CARD_AUTO)
    card = audio_hw_next_card++;
  if(audio_find_hw_device(card, device, direction) != 0){
    release(&audio_core.lock);
    return 0;
  }
  if(audio_hw_count >= AUDIO_MAX_DEVICES){
    release(&audio_core.lock);
    return -1;
  }

  slot = &audio_hw_devs[audio_hw_count++];
  memset(slot, 0, sizeof(*slot));
  slot->vendor_id = vendor_id;
  slot->device_id = device_id;
  slot->card = card;
  slot->device = device;
  slot->direction = direction;
  slot->flags = flags;
  slot->hw_profile = hw_profile;
  release(&audio_core.lock);

  BOOTDBG("audio: registered %s card=%d dev=%d dir=%d ven=%x dev=%x profile=%d\n",
          driver_name ? driver_name : "audio-hw",
          card, device, direction, vendor_id, device_id, hw_profile);
  return 0;
}
