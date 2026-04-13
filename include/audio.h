/*
 * Audio subsystem public ABI types.
 *
 * Stage 0 contract source: docs/audio-stage0-contract-pack.md
 */

#ifndef AUXV6_AUDIO_H
#define AUXV6_AUDIO_H

#include "stdint.h"

#define AUDIO_ABI_MAJOR 1
#define AUDIO_ABI_MINOR 0
#define AUDIO_ABI_PATCH 0
#define AUDIO_ABI_VERSION ((AUDIO_ABI_MAJOR << 16) | (AUDIO_ABI_MINOR << 8) | AUDIO_ABI_PATCH)

#define AUDIO_MAX_DEVICES      32
#define AUDIO_MAX_FORMATS       8
#define AUDIO_MAX_RATES        16
#define AUDIO_CARD_AUTO     0xFFFFU

#define AUDIO_VOL_MIN_DB_Q8_8 (-9600)
#define AUDIO_VOL_MAX_DB_Q8_8 (0)

#define AUDIO_DEFAULT_RATE_HZ       48000
#define AUDIO_DEFAULT_CHANNELS          2
#define AUDIO_DEFAULT_PERIOD_FRAMES   256
#define AUDIO_DEFAULT_PERIODS           4
#define AUDIO_DEFAULT_BUFFER_FRAMES  (AUDIO_DEFAULT_PERIOD_FRAMES * AUDIO_DEFAULT_PERIODS)

enum audio_direction {
  AUDIO_DIR_PLAYBACK = 0,
  AUDIO_DIR_CAPTURE  = 1,
};

enum audio_sample_format {
  AUDIO_FMT_S16_LE = 0,
  AUDIO_FMT_S24_LE = 1,
  AUDIO_FMT_S32_LE = 2,
  AUDIO_FMT_U8     = 3,
};

enum audio_stream_state {
  AUDIO_ST_NEW        = 0,
  AUDIO_ST_CONFIGURED = 1,
  AUDIO_ST_PREPARED   = 2,
  AUDIO_ST_RUNNING    = 3,
  AUDIO_ST_XRUN       = 4,
  AUDIO_ST_STOPPED    = 5,
  AUDIO_ST_DRAINED    = 6,
  AUDIO_ST_CLOSED     = 7,
};

/* Device and stream flags (reserved for incremental expansion). */
#define AUDIO_DEVF_CAN_PLAYBACK 0x00000001U
#define AUDIO_DEVF_CAN_CAPTURE  0x00000002U
#define AUDIO_DEVF_OSS_DSP_COMPAT   0x00010000U
#define AUDIO_DEVF_OSS_MIXER_COMPAT 0x00020000U

#define AUDIO_STF_NONBLOCK      0x00000001U
#define AUDIO_STF_EXCLUSIVE     0x00000002U

/*
 * Kernel-internal hardware profile tags used by probe stubs to seed
 * conservative capability defaults until real backends are wired.
 */
enum audio_hw_profile {
  AUDIO_HW_PROFILE_NULL = 0,
  AUDIO_HW_PROFILE_AC97 = 1,
  AUDIO_HW_PROFILE_HDA = 2,
  AUDIO_HW_PROFILE_LEGACY_PCI_PCM = 3,
};

struct audio_abi_info {
  uint32_t abi_version;
  uint32_t struct_size;
  uint16_t abi_major;
  uint16_t abi_minor;
  uint16_t abi_patch;
  uint16_t reserved0;
  uint32_t reserved1[4];
};

struct audio_device_id {
  uint16_t card;
  uint16_t device;
  uint16_t direction;  /* enum audio_direction */
  uint16_t reserved0;
};

struct audio_device_info {
  uint32_t abi_version;
  uint32_t struct_size;

  uint16_t card;
  uint16_t device;
  uint16_t direction;
  uint16_t reserved0;

  uint32_t flags;      /* AUDIO_DEVF_* */
  uint32_t reserved1[5];
};

struct audio_enum_devices {
  uint32_t abi_version;
  uint32_t struct_size;

  uint32_t max_entries;
  uint32_t num_entries;

  uint64_t entries_ptr;  /* user pointer to array of struct audio_device_info */

  uint32_t reserved0[6];
};

struct audio_default_route {
  uint32_t abi_version;
  uint32_t struct_size;

  uint16_t card;
  uint16_t device;
  uint16_t direction;
  uint16_t reserved0;

  uint32_t reserved1[6];
};

struct audio_hw_caps {
  uint32_t abi_version;
  uint32_t struct_size;

  uint16_t card;
  uint16_t device;
  uint16_t direction;
  uint16_t reserved0;

  uint32_t min_channels;
  uint32_t max_channels;
  uint32_t min_rate;
  uint32_t max_rate;

  uint32_t format_count;
  uint32_t formats[AUDIO_MAX_FORMATS];

  uint32_t rate_count;
  uint32_t rates[AUDIO_MAX_RATES];

  uint32_t min_period_frames;
  uint32_t max_period_frames;
  uint32_t min_periods;
  uint32_t max_periods;

  uint32_t flags;
  uint32_t reserved1[7];
};

struct audio_stream_params {
  uint32_t abi_version;
  uint32_t struct_size;

  uint32_t sample_rate;
  uint32_t channels;
  uint32_t sample_format;   /* enum audio_sample_format */

  uint32_t period_frames;
  uint32_t periods;
  uint32_t buffer_frames;   /* 0 means derive from period_frames * periods */

  uint32_t flags;           /* AUDIO_STF_* */
  uint32_t reserved0[8];
};

struct audio_stream_status {
  uint32_t abi_version;
  uint32_t struct_size;

  uint32_t state;           /* enum audio_stream_state */
  uint32_t flags;

  uint64_t hw_ptr_frames;
  uint64_t sw_ptr_frames;
  uint64_t queued_frames;
  uint64_t delay_frames;

  uint32_t xruns;
  uint32_t late_wakeups;
  uint32_t period_misses;
  uint32_t recoveries;

  uint64_t monotonic_ns;
  uint32_t reserved0[6];
};

struct audio_stream_volume {
  uint32_t abi_version;
  uint32_t struct_size;

  int32_t left_db_q8_8;
  int32_t right_db_q8_8;

  uint32_t mute;
  uint32_t reserved0[7];
};

struct audio_cmd {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t flags;
  uint32_t reserved0;
};

#endif /* AUXV6_AUDIO_H */
