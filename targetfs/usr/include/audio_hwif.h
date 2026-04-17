/*
 * Audio hardware abstraction interface (hwif).
 *
 * Defines struct audio_hw_ops, the formal contract between the generic audio
 * core (audio_core.c) and hardware-specific backends (AC97, HDA, etc.).
 *
 * Modeled on NetBSD audio(9) audio_hw_if; simplified for clarity.
 *
 * Backends implement this interface and register via audio_attach_hwif(),
 * which decouples core logic from hardware implementation details.
 */

#ifndef AUXV6_AUDIO_HWIF_H
#define AUXV6_AUDIO_HWIF_H

#include "stdint.h"

/*
 * struct audio_format - Describes a single audio format configuration.
 * Used by query_format() and set_format() callbacks.
 */
struct audio_format {
  uint32_t sample_rate;      /* Hz: 8000, 16000, 48000, 192000, etc. */
  uint32_t channels;         /* 1, 2, 4, 6, 8 */
  uint32_t sample_format;    /* enum audio_sample_format (S16_LE, etc.) */
  uint32_t reserved0;
};

/*
 * struct audio_props - Hardware capability properties.
 * Returned by get_props() callback.
 */
struct audio_props {
  uint32_t min_rate;
  uint32_t max_rate;
  uint32_t min_channels;
  uint32_t max_channels;
  uint32_t formats[8];       /* enum audio_sample_format, 0 = end */
  uint32_t rates[16];        /* Discrete rates in Hz, 0 = end; 0xffffffff = range */
  uint32_t flags;            /* Reserved for future capabilities */
  uint32_t reserved0[4];
};

/*
 * struct audio_hw_ops - Hardware abstraction interface.
 *
 * All callbacks receive `hdl` (opaque hardware handle) as the first argument.
 * Callbacks may be called from interrupt context or process context depending
 * on the callback type; see notes below.
 *
 * Callbacks return 0 on success, negative error code on failure.
 * pointer() returns uint32_t frame offset (not an error code).
 */
struct audio_hw_ops {
  /*
   * Format and parameter negotiation.
   * Called from process context during stream setup.
   */

  /* query_format: Return 0 if hardware supports `fmt`, -1 otherwise.
   * Used to validate client-requested formats before accepting them. */
  int (*query_format)(void *hdl, struct audio_format *fmt);

  /* set_format: Configure hardware for the given format.
   * Called whenever params are changed (stream state: CONFIGURED or PREPARED). */
  int (*set_format)(void *hdl, struct audio_format *fmt);

  /* round_blocksize: Round the requested block size to hardware granule.
   * Returns rounded size, or -1 on error.
   * mode: AUMODE_PLAY or AUMODE_RECORD.
   * Allows hardware to enforce alignment (e.g., 64-byte blocks). */
  int (*round_blocksize)(void *hdl, int bs, int mode);

  /*
   * Stream lifecycle.
   * Called from process context during ioctl(PREPARE, START, STOP, etc.).
   * Some may be called from interrupt (e.g., halt_*) in exceptional cases.
   */

  /* init_output: Prepare playback hardware for streaming.
   * Called when transitioning from CONFIGURED to PREPARED. */
  int (*init_output)(void *hdl);

  /* init_input: Prepare capture hardware for streaming.
   * Called when transitioning from CONFIGURED to PREPARED. */
  int (*init_input)(void *hdl);

  /* trigger_output: Start playback DMA and interrupt generation.
   * Called when transitioning from PREPARED to RUNNING. */
  int (*trigger_output)(void *hdl);

  /* trigger_input: Start capture DMA and interrupt generation.
   * Called when transitioning from PREPARED to RUNNING. */
  int (*trigger_input)(void *hdl);

  /* halt_output: Stop playback DMA and interrupt generation.
   * Called when transitioning from RUNNING back to PREPARED or STOPPED. */
  int (*halt_output)(void *hdl);

  /* halt_input: Stop capture DMA and interrupt generation.
   * Called when transitioning from RUNNING back to PREPARED or STOPPED. */
  int (*halt_input)(void *hdl);

  /*
   * DMA position query.
   * Called from interrupt context (high-frequency); must be fast.
   */

  /* pointer: Return current DMA position in frames since stream start.
   * mode: AUMODE_PLAY or AUMODE_RECORD.
   * This is the authoritative hardware pointer; audio_core.c reads this
   * to determine how much data has been consumed/produced.
   * Called from interrupt context; must not sleep or acquire locks. */
  uint32_t (*pointer)(void *hdl, int mode);

  /*
   * Hardware properties and capabilities.
   * Called from process context during device enumeration (QUERY_CAPS, etc.).
   */

  /* get_props: Fill struct audio_props with hardware capabilities.
   * Returns 0 on success, -1 on error.
   * Provides min/max rates, channel counts, format list, etc. */
  int (*get_props)(void *hdl, struct audio_props *props);

  /*
   * Memory management.
   * Called to allocate/deallocate DMA-safe buffers.
   */

  /* allocm: Allocate `size` bytes of DMA-safe memory.
   * type: 0 (reserved for future use).
   * Returns pointer to allocated memory, or NULL if allocation fails.
   * Returned buffer must be DMA-safe (48-bit addressable, aligned). */
  void *(*allocm)(void *hdl, int direction, uint32_t size, int type);

  /* freem: Free memory previously allocated by allocm().
   * addr: pointer returned by allocm().
   * type: same value passed to allocm(). */
  void (*freem)(void *hdl, void *addr, int type);

  /*
   * Mixer controls (optional).
   * Called from process context during mixer ioctl operations.
   * If not implemented, set to NULL; core will return -1 for mixer calls.
   */

  /* mixer_set_port: Set mixer control value.
   * mc: pointer to mixer_ctrl_t struct (format TBD; stub for now).
   * Returns 0 on success, -1 on error. */
  int (*mixer_set_port)(void *hdl, void *mc);

  /* mixer_get_port: Get mixer control value.
   * mc: pointer to mixer_ctrl_t struct.
   * Fills in current control values.
   * Returns 0 on success, -1 on error. */
  int (*mixer_get_port)(void *hdl, void *mc);

  /*
   * Reserved for future use (ABI expansion).
   */
  uint32_t reserved[8];
};

/*
 * Audio mode indicators (for init_output/halt_output, etc.).
 */
#define AUMODE_PLAY     0
#define AUMODE_RECORD   1

/*
 * Kernel-internal structure associating a hardware backend with its ops.
 * Created by backends and passed to audio_attach_hwif().
 */
struct audio_hw_device {
  uint16_t vendor_id;
  uint16_t device_id;
  uint16_t card;
  uint16_t device;
  uint16_t direction;
  uint16_t reserved0;
  uint32_t flags;
  uint32_t hw_profile;

  /* New fields (not in old audio_hw_device): */
  struct audio_hw_ops *ops;      /* Operations vector */
  void *hdl;                     /* Hardware handle (e.g., &ac97_softc) */
  struct audio_format current_format;  /* Last negotiated format */
};

#endif /* AUXV6_AUDIO_HWIF_H */
