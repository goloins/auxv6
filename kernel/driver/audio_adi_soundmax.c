#include "types.h"
#include "defs.h"
#include "pci.h"
#include "audio.h"
#include "audio_pci_common.h"

#define AUDIO_ADI_SOUNDMAX_MAX 4

static struct audio_pci_stub_softc sc[AUDIO_ADI_SOUNDMAX_MAX];
static int count;

static const struct audio_pci_stub_match match_tbl[] = {
  { "adi-soundmax", PCI_VENDOR_ANALOG_DEVICES, AUDIO_PCI_ANY_DEVICE,
    AUDIO_PCI_ANY_CLASS, AUDIO_PCI_ANY_SUBCLASS,
    AUDIO_DEVF_CAN_PLAYBACK | AUDIO_DEVF_OSS_DSP_COMPAT |
    AUDIO_DEVF_OSS_MIXER_COMPAT,
    AUDIO_HW_PROFILE_AC97 },
};

void
audio_adi_soundmax_init(void)
{
  int i;
  BOOTDBG("audio/adi-soundmax: probe\n");
  for(i = 0; i < pci_device_count() && count < AUDIO_ADI_SOUNDMAX_MAX; i++){
    struct pci_dev *dev = pci_get_device(i);
    if(!audio_pci_stub_match_dev(&match_tbl[0], dev))
      continue;
    if(audio_pci_stub_attach(&sc[count], dev, &match_tbl[0], 0) == 0)
      count++;
  }
}
