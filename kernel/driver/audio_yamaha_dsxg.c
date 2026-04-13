#include "types.h"
#include "defs.h"
#include "pci.h"
#include "audio.h"
#include "audio_pci_common.h"

#define AUDIO_YAMAHA_DSXG_MAX 4

static struct audio_pci_stub_softc sc[AUDIO_YAMAHA_DSXG_MAX];
static int count;

static const struct audio_pci_stub_match match_tbl[] = {
  { "yamaha-dsxg", PCI_VENDOR_YAMAHA, AUDIO_PCI_ANY_DEVICE,
    PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_MULTIMEDIA_AUDIO,
    AUDIO_DEVF_CAN_PLAYBACK | AUDIO_DEVF_OSS_DSP_COMPAT |
    AUDIO_DEVF_OSS_MIXER_COMPAT,
    AUDIO_HW_PROFILE_LEGACY_PCI_PCM },
};

void
audio_yamaha_dsxg_init(void)
{
  int i;
  BOOTDBG("audio/yamaha-dsxg: probe\n");
  for(i = 0; i < pci_device_count() && count < AUDIO_YAMAHA_DSXG_MAX; i++){
    struct pci_dev *dev = pci_get_device(i);
    if(!audio_pci_stub_match_dev(&match_tbl[0], dev))
      continue;
    if(audio_pci_stub_attach(&sc[count], dev, &match_tbl[0], 1) == 0)
      count++;
  }
}
