/*
 * PCI audio-family orchestration for Stage-0 stubs.
 *
 * Each common family lives in its own driver file, mirroring the NIC layout.
 */

#include "types.h"
#include "defs.h"

void
audio_pci_probe_init(void)
{
  BOOTDBG("audio: probing split PCI audio-family stubs\n");
  audio_intel_ac97_init();
  audio_realtek_ac97_init();
  audio_creative_live_init();
  audio_creative_audigy_init();
  audio_cmedia_cm8738_init();
  audio_via_envy24_init();
  audio_yamaha_dsxg_init();
  audio_ess_maestro_init();
  audio_adi_soundmax_init();
  audio_sigmatel_hda_init();
  audio_intel_hda_init();
  audio_realtek_hda_init();
  audio_conexant_hda_init();
  audio_nvidia_mcp_init();
  audio_creative_xfi_init();
}
