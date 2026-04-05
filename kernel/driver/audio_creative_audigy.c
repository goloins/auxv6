#include "types.h"
#include "defs.h"
#include "pci.h"
#include "audio.h"
#include "audio_pci_common.h"

#define AUDIO_CREATIVE_AUDIGY_MAX 4

static struct audio_pci_stub_softc sc[AUDIO_CREATIVE_AUDIGY_MAX];
static int count;

static const struct audio_pci_stub_match match_tbl[] = {
  { "creative-audigy", PCI_VENDOR_CREATIVE, 0x0004,
    AUDIO_PCI_ANY_CLASS, AUDIO_PCI_ANY_SUBCLASS,
    AUDIO_DEVF_CAN_PLAYBACK | AUDIO_DEVF_OSS_DSP_COMPAT |
    AUDIO_DEVF_OSS_MIXER_COMPAT,
    AUDIO_HW_PROFILE_LEGACY_PCI_PCM },
};

void
audio_creative_audigy_init(void)
{
  int i;
  BOOTDBG("audio/creative-audigy: probe\n");
  for(i = 0; i < pci_device_count() && count < AUDIO_CREATIVE_AUDIGY_MAX; i++){
    struct pci_dev *dev = pci_get_device(i);
    if(!audio_pci_stub_match_dev(&match_tbl[0], dev))
      continue;
    if(audio_pci_stub_attach(&sc[count], dev, &match_tbl[0], 1) == 0)
      count++;
  }
}
