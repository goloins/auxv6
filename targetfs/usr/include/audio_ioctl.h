/*
 * Audio subsystem ioctl request numbers.
 *
 * Stage 0 contract source: docs/audio-stage0-contract-pack.md
 */

#ifndef AUXV6_AUDIO_IOCTL_H
#define AUXV6_AUDIO_IOCTL_H

#include "audio.h"

/*
 * Native audio ioctl number space.
 *
 * Existing tree uses fixed request constants (for example TCGETS 0x5401),
 * so audio follows the same style for predictable ABI stability.
 */
#define AUDIO_IOC_BASE        0x5600

/* /dev/audioctl commands */
#define AUDIO_IOC_QUERY_ABI   0x5600  /* in/out: struct audio_abi_info */
#define AUDIO_IOC_ENUM_DEVICES 0x5601 /* in/out: struct audio_enum_devices */
#define AUDIO_IOC_QUERY_CAPS  0x5602  /* in/out: struct audio_hw_caps */
#define AUDIO_IOC_SET_DEFAULT 0x5603  /* in:     struct audio_default_route */
#define AUDIO_IOC_GET_DEFAULT 0x5604  /* in/out: struct audio_default_route */

/* /dev/pcm* stream commands */
#define AUDIO_IOC_SET_PARAMS  0x5610  /* in:     struct audio_stream_params */
#define AUDIO_IOC_GET_PARAMS  0x5611  /* in/out: struct audio_stream_params */
#define AUDIO_IOC_PREPARE     0x5612  /* in/out: struct audio_cmd */
#define AUDIO_IOC_START       0x5613  /* in/out: struct audio_cmd */
#define AUDIO_IOC_STOP        0x5614  /* in/out: struct audio_cmd */
#define AUDIO_IOC_DRAIN       0x5615  /* in/out: struct audio_cmd */
#define AUDIO_IOC_DROP        0x5616  /* in/out: struct audio_cmd */
#define AUDIO_IOC_GET_STATUS  0x5617  /* in/out: struct audio_stream_status */
#define AUDIO_IOC_SET_STREAM_VOL 0x5618 /* in:   struct audio_stream_volume */
#define AUDIO_IOC_GET_STREAM_VOL 0x5619 /* in/out: struct audio_stream_volume */
#define AUDIO_IOC_RESET_XRUN  0x561A  /* in/out: struct audio_cmd */

/* Reserved compatibility control range. */
#define AUDIO_IOC_COMPAT_BASE 0x5680
#define AUDIO_IOC_COMPAT_END  0x56FF

#endif /* AUXV6_AUDIO_IOCTL_H */
