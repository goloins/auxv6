#include "types.h"
#include "fcntl.h"
#include "sys/ioctl.h"
#include "audio.h"
#include "auxv6/user.h"

#define AUDIOCTL_PATH "/dev/audioctl"
#define AUDIOPCM_PATH "/dev/pcmC0D0p"

static void
usage(void)
{
  dprintf(2,
          "usage:\n"
          "  audioctl abi [audioctl-dev]\n"
          "  audioctl enum [audioctl-dev]\n"
          "  audioctl caps [audioctl-dev]\n"
          "  audioctl default [audioctl-dev]\n"
          "  audioctl set-default <card> <device> <dir> [audioctl-dev]\n"
          "  audioctl params [pcm-dev]\n"
          "  audioctl set-params <rate> <channels> <format> <period_frames> <periods> <buffer_frames> [pcm-dev]\n"
          "  audioctl prepare|start|stop|drain|drop|reset-xrun [pcm-dev]\n"
          "  audioctl status [pcm-dev]\n"
          "  audioctl volume [pcm-dev]\n"
          "  audioctl set-volume <left_q8_8> <right_q8_8> <mute> [pcm-dev]\n");
  exit(1);
}

static int
open_dev(const char *path)
{
  int fd;

  fd = open(path, O_RDWR);
  if(fd < 0)
    dprintf(2, "audioctl: cannot open %s\n", path);
  return fd;
}

static int
cmd_abi(const char *path)
{
  struct audio_abi_info abi;
  int fd;

  fd = open_dev(path);
  if(fd < 0)
    return 1;

  memset(&abi, 0, sizeof(abi));
  abi.abi_version = AUDIO_ABI_VERSION;
  abi.struct_size = sizeof(abi);
  if(ioctl(fd, AUDIO_IOC_QUERY_ABI, &abi) < 0){
    dprintf(2, "audioctl: AUDIO_IOC_QUERY_ABI failed\n");
    close(fd);
    return 1;
  }

  dprintf(1, "abi_version %d\n", abi.abi_version);
  dprintf(1, "abi_major %d\n", abi.abi_major);
  dprintf(1, "abi_minor %d\n", abi.abi_minor);
  dprintf(1, "abi_patch %d\n", abi.abi_patch);

  close(fd);
  return 0;
}

static int
cmd_caps(const char *path)
{
  struct audio_hw_caps caps;
  int fd;
  uint i;

  fd = open_dev(path);
  if(fd < 0)
    return 1;

  memset(&caps, 0, sizeof(caps));
  caps.abi_version = AUDIO_ABI_VERSION;
  caps.struct_size = sizeof(caps);
  if(ioctl(fd, AUDIO_IOC_QUERY_CAPS, &caps) < 0){
    dprintf(2, "audioctl: AUDIO_IOC_QUERY_CAPS failed\n");
    close(fd);
    return 1;
  }

  dprintf(1, "channels %d..%d\n", caps.min_channels, caps.max_channels);
  dprintf(1, "rate %d..%d\n", caps.min_rate, caps.max_rate);
  dprintf(1, "period_frames %d..%d\n", caps.min_period_frames, caps.max_period_frames);
  dprintf(1, "periods %d..%d\n", caps.min_periods, caps.max_periods);
  dprintf(1, "flags %d\n", caps.flags);

  dprintf(1, "formats");
  for(i = 0; i < caps.format_count && i < AUDIO_MAX_FORMATS; i++)
    dprintf(1, " %d", caps.formats[i]);
  dprintf(1, "\n");

  dprintf(1, "rates");
  for(i = 0; i < caps.rate_count && i < AUDIO_MAX_RATES; i++)
    dprintf(1, " %d", caps.rates[i]);
  dprintf(1, "\n");

  close(fd);
  return 0;
}

static int
cmd_enum(const char *path)
{
  struct audio_enum_devices req;
  struct audio_device_info list[8];
  int fd;
  uint i;
  uint limit;

  fd = open_dev(path);
  if(fd < 0)
    return 1;

  memset(&req, 0, sizeof(req));
  memset(list, 0, sizeof(list));
  req.abi_version = AUDIO_ABI_VERSION;
  req.struct_size = sizeof(req);
  req.max_entries = (uint32_t)(sizeof(list) / sizeof(list[0]));
  req.entries_ptr = (uint64_t)(uint)list;

  if(ioctl(fd, AUDIO_IOC_ENUM_DEVICES, &req) < 0){
    dprintf(2, "audioctl: AUDIO_IOC_ENUM_DEVICES failed\n");
    close(fd);
    return 1;
  }

  dprintf(1, "num_entries %d\n", req.num_entries);
  limit = req.num_entries;
  if(limit > req.max_entries)
    limit = req.max_entries;

  for(i = 0; i < limit; i++){
    dprintf(1, "dev[%d] card=%d device=%d dir=%d flags=%d\n",
            i, list[i].card, list[i].device, list[i].direction, list[i].flags);
  }

  close(fd);
  return 0;
}

static int
cmd_default_get(const char *path)
{
  struct audio_default_route route;
  int fd;

  fd = open_dev(path);
  if(fd < 0)
    return 1;

  memset(&route, 0, sizeof(route));
  route.abi_version = AUDIO_ABI_VERSION;
  route.struct_size = sizeof(route);
  if(ioctl(fd, AUDIO_IOC_GET_DEFAULT, &route) < 0){
    dprintf(2, "audioctl: AUDIO_IOC_GET_DEFAULT failed\n");
    close(fd);
    return 1;
  }

  dprintf(1, "card %d\n", route.card);
  dprintf(1, "device %d\n", route.device);
  dprintf(1, "direction %d\n", route.direction);

  close(fd);
  return 0;
}

static int
cmd_default_set(int card, int dev, int dir, const char *path)
{
  struct audio_default_route route;
  int fd;

  fd = open_dev(path);
  if(fd < 0)
    return 1;

  memset(&route, 0, sizeof(route));
  route.abi_version = AUDIO_ABI_VERSION;
  route.struct_size = sizeof(route);
  route.card = (uint16_t)card;
  route.device = (uint16_t)dev;
  route.direction = (uint16_t)dir;

  if(ioctl(fd, AUDIO_IOC_SET_DEFAULT, &route) < 0){
    dprintf(2, "audioctl: AUDIO_IOC_SET_DEFAULT failed\n");
    close(fd);
    return 1;
  }

  dprintf(1, "ok\n");
  close(fd);
  return 0;
}

static int
cmd_params(const char *path)
{
  struct audio_stream_params params;
  int fd;

  fd = open_dev(path);
  if(fd < 0)
    return 1;

  memset(&params, 0, sizeof(params));
  params.abi_version = AUDIO_ABI_VERSION;
  params.struct_size = sizeof(params);
  if(ioctl(fd, AUDIO_IOC_GET_PARAMS, &params) < 0){
    dprintf(2, "audioctl: AUDIO_IOC_GET_PARAMS failed\n");
    close(fd);
    return 1;
  }

  dprintf(1, "rate %d\n", params.sample_rate);
  dprintf(1, "channels %d\n", params.channels);
  dprintf(1, "format %d\n", params.sample_format);
  dprintf(1, "period_frames %d\n", params.period_frames);
  dprintf(1, "periods %d\n", params.periods);
  dprintf(1, "buffer_frames %d\n", params.buffer_frames);

  close(fd);
  return 0;
}

static int
cmd_status(const char *path)
{
  struct audio_stream_status st;
  int fd;

  fd = open_dev(path);
  if(fd < 0)
    return 1;

  memset(&st, 0, sizeof(st));
  st.abi_version = AUDIO_ABI_VERSION;
  st.struct_size = sizeof(st);
  if(ioctl(fd, AUDIO_IOC_GET_STATUS, &st) < 0){
    dprintf(2, "audioctl: AUDIO_IOC_GET_STATUS failed\n");
    close(fd);
    return 1;
  }

  dprintf(1, "state %d\n", st.state);
  dprintf(1, "flags %d\n", st.flags);
  dprintf(1, "queued_frames %d\n", (uint)st.queued_frames);
  dprintf(1, "delay_frames %d\n", (uint)st.delay_frames);
  dprintf(1, "xruns %d\n", st.xruns);
  dprintf(1, "late_wakeups %d\n", st.late_wakeups);
  dprintf(1, "period_misses %d\n", st.period_misses);
  dprintf(1, "recoveries %d\n", st.recoveries);

  close(fd);
  return 0;
}

static int
cmd_set_params(int rate, int channels, int format, int period_frames,
               int periods, int buffer_frames, const char *path)
{
  struct audio_stream_params params;
  int fd;

  fd = open_dev(path);
  if(fd < 0)
    return 1;

  memset(&params, 0, sizeof(params));
  params.abi_version = AUDIO_ABI_VERSION;
  params.struct_size = sizeof(params);
  params.sample_rate = (uint32_t)rate;
  params.channels = (uint16_t)channels;
  params.sample_format = (uint16_t)format;
  params.period_frames = (uint32_t)period_frames;
  params.periods = (uint16_t)periods;
  params.buffer_frames = (uint32_t)buffer_frames;

  if(ioctl(fd, AUDIO_IOC_SET_PARAMS, &params) < 0){
    dprintf(2, "audioctl: AUDIO_IOC_SET_PARAMS failed\n");
    close(fd);
    return 1;
  }

  dprintf(1, "ok\n");
  close(fd);
  return 0;
}

static int
cmd_simple_ioctl(const char *path, uint32_t req, const char *name)
{
  int fd;

  fd = open_dev(path);
  if(fd < 0)
    return 1;

  if(ioctl(fd, req, 0) < 0){
    dprintf(2, "audioctl: %s failed\n", name);
    close(fd);
    return 1;
  }

  dprintf(1, "ok\n");
  close(fd);
  return 0;
}

static int
cmd_volume_get(const char *path)
{
  struct audio_stream_volume vol;
  int fd;

  fd = open_dev(path);
  if(fd < 0)
    return 1;

  memset(&vol, 0, sizeof(vol));
  vol.abi_version = AUDIO_ABI_VERSION;
  vol.struct_size = sizeof(vol);
  if(ioctl(fd, AUDIO_IOC_GET_STREAM_VOL, &vol) < 0){
    dprintf(2, "audioctl: AUDIO_IOC_GET_STREAM_VOL failed\n");
    close(fd);
    return 1;
  }

  dprintf(1, "left_db_q8_8 %d\n", vol.left_db_q8_8);
  dprintf(1, "right_db_q8_8 %d\n", vol.right_db_q8_8);
  dprintf(1, "mute %d\n", vol.mute);

  close(fd);
  return 0;
}

static int
cmd_volume_set(int l, int r, int mute, const char *path)
{
  struct audio_stream_volume vol;
  int fd;

  fd = open_dev(path);
  if(fd < 0)
    return 1;

  memset(&vol, 0, sizeof(vol));
  vol.abi_version = AUDIO_ABI_VERSION;
  vol.struct_size = sizeof(vol);
  vol.left_db_q8_8 = l;
  vol.right_db_q8_8 = r;
  vol.mute = mute ? 1U : 0U;

  if(ioctl(fd, AUDIO_IOC_SET_STREAM_VOL, &vol) < 0){
    dprintf(2, "audioctl: AUDIO_IOC_SET_STREAM_VOL failed\n");
    close(fd);
    return 1;
  }

  dprintf(1, "ok\n");
  close(fd);
  return 0;
}

int
main(int argc, char *argv[])
{
  if(argc < 2)
    usage();

  if(strcmp(argv[1], "abi") == 0)
    return cmd_abi(argc >= 3 ? argv[2] : AUDIOCTL_PATH);
  if(strcmp(argv[1], "enum") == 0)
    return cmd_enum(argc >= 3 ? argv[2] : AUDIOCTL_PATH);
  if(strcmp(argv[1], "caps") == 0)
    return cmd_caps(argc >= 3 ? argv[2] : AUDIOCTL_PATH);
  if(strcmp(argv[1], "default") == 0)
    return cmd_default_get(argc >= 3 ? argv[2] : AUDIOCTL_PATH);
  if(strcmp(argv[1], "set-default") == 0){
    if(argc < 5)
      usage();
    return cmd_default_set(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]),
                           argc >= 6 ? argv[5] : AUDIOCTL_PATH);
  }
  if(strcmp(argv[1], "params") == 0)
    return cmd_params(argc >= 3 ? argv[2] : AUDIOPCM_PATH);
  if(strcmp(argv[1], "set-params") == 0){
    if(argc < 8)
      usage();
    return cmd_set_params(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]),
                          atoi(argv[5]), atoi(argv[6]), atoi(argv[7]),
                          argc >= 9 ? argv[8] : AUDIOPCM_PATH);
  }
  if(strcmp(argv[1], "prepare") == 0)
    return cmd_simple_ioctl(argc >= 3 ? argv[2] : AUDIOPCM_PATH,
                            AUDIO_IOC_PREPARE, "AUDIO_IOC_PREPARE");
  if(strcmp(argv[1], "start") == 0)
    return cmd_simple_ioctl(argc >= 3 ? argv[2] : AUDIOPCM_PATH,
                            AUDIO_IOC_START, "AUDIO_IOC_START");
  if(strcmp(argv[1], "stop") == 0)
    return cmd_simple_ioctl(argc >= 3 ? argv[2] : AUDIOPCM_PATH,
                            AUDIO_IOC_STOP, "AUDIO_IOC_STOP");
  if(strcmp(argv[1], "drain") == 0)
    return cmd_simple_ioctl(argc >= 3 ? argv[2] : AUDIOPCM_PATH,
                            AUDIO_IOC_DRAIN, "AUDIO_IOC_DRAIN");
  if(strcmp(argv[1], "drop") == 0)
    return cmd_simple_ioctl(argc >= 3 ? argv[2] : AUDIOPCM_PATH,
                            AUDIO_IOC_DROP, "AUDIO_IOC_DROP");
  if(strcmp(argv[1], "reset-xrun") == 0)
    return cmd_simple_ioctl(argc >= 3 ? argv[2] : AUDIOPCM_PATH,
                            AUDIO_IOC_RESET_XRUN, "AUDIO_IOC_RESET_XRUN");
  if(strcmp(argv[1], "status") == 0)
    return cmd_status(argc >= 3 ? argv[2] : AUDIOPCM_PATH);
  if(strcmp(argv[1], "volume") == 0)
    return cmd_volume_get(argc >= 3 ? argv[2] : AUDIOPCM_PATH);
  if(strcmp(argv[1], "set-volume") == 0){
    if(argc < 5)
      usage();
    return cmd_volume_set(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]),
                          argc >= 6 ? argv[5] : AUDIOPCM_PATH);
  }

  usage();
  return 1;
}
