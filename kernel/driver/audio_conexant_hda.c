#include "types.h"
#include "defs.h"
#include "pci.h"
#include "audio.h"
#include "audio_pci_common.h"

#define AUDIO_CONEXANT_HDA_MAX 4

static struct audio_pci_stub_softc sc[AUDIO_CONEXANT_HDA_MAX];
static int count;

static const struct audio_pci_stub_match match_tbl[] = {
  { "conexant-hda", PCI_VENDOR_CONEXANT, AUDIO_PCI_ANY_DEVICE,
    PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_MULTIMEDIA_HDA,
    AUDIO_DEVF_CAN_PLAYBACK | AUDIO_DEVF_OSS_DSP_COMPAT |
    AUDIO_DEVF_OSS_MIXER_COMPAT,
    AUDIO_HW_PROFILE_HDA },
};

void
audio_conexant_hda_init(void)
{
  int i;
  BOOTDBG("audio/conexant-hda: probe\n");
  for(i = 0; i < pci_device_count() && count < AUDIO_CONEXANT_HDA_MAX; i++){
    struct pci_dev *dev = pci_get_device(i);
    if(!audio_pci_stub_match_dev(&match_tbl[0], dev))
      continue;
    if(audio_pci_stub_attach(&sc[count], dev, &match_tbl[0], 1) == 0)
      count++;
  }
}
