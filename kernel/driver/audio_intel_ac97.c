/*
 * audio_intel_ac97.c — Intel i82801AA AC97 audio driver
 *
 * Supports QEMU -device AC97 (Intel i82801AA, PCI 8086:2415).
 * Implements PCM-Out DMA via the Native Audio Bus Master (NABM) interface
 * using a ping-pong Buffer Descriptor List (BDL) of AC97_BDL_USE slots.
 *
 * Hardware reference: Intel ICH AC97 specification, section 5.
 *
 * Architecture:
 *   - Two static 2 KB DMA bounce buffers (AC97_BDL_USE × AC97_PERIOD_BYTES)
 *   - BDL of AC97_BDL_USE entries; LVI is advanced on each completion ISR
 *   - Each DMA completion interrupt: call audio_hw_period_advance() to pull
 *     the next period from the kernel ring buffer into the completed slot,
 *     then bump LVI.
 *   - ring_size=4096, period=2048 bytes → ~21ms per period at 48kHz/2ch/S16LE
 */

#include "types.h"
#include "defs.h"
#include "pci.h"
#include "audio.h"
#include "audio_pci_common.h"
#include "x86.h"
#include "memlayout.h"
#include "traps.h"

/* -----------------------------------------------------------------------
 * AC97 NAMBAR register offsets (codec shadow, word-width I/O reads/writes)
 * ----------------------------------------------------------------------- */
#define AC97_RESET          0x00
#define AC97_MASTER_VOL     0x02
#define AC97_MONO_VOL       0x06
#define AC97_PCM_OUT_VOL    0x18
#define AC97_EXT_AUDIO_ID   0x28
#define AC97_EXT_AUDIO_CTL  0x2A
#define AC97_PCM_FRONT_RATE 0x2C

/* -----------------------------------------------------------------------
 * AC97 NABM register offsets (PCM-Out channel base = 0x10)
 * ----------------------------------------------------------------------- */
#define NABM_PCM_OUT_BASE   0x10
#define NABM_OFF_BDBAR      0x00
#define NABM_OFF_CIV        0x04
#define NABM_OFF_LVI        0x05
#define NABM_OFF_SR         0x06
#define NABM_OFF_PICB       0x08
#define NABM_OFF_PIV        0x0A
#define NABM_OFF_CR         0x0B

#define NABM_GLOB_CNT       0x2C
#define NABM_GLOB_STA       0x30

/* SR bits */
#define NABM_SR_DCH         0x0001
#define NABM_SR_CELV        0x0002
#define NABM_SR_LVBCI       0x0004
#define NABM_SR_BCIS        0x0008
#define NABM_SR_FIFOE       0x0010

/* CR bits */
#define NABM_CR_RPBM        0x01
#define NABM_CR_RR          0x02
#define NABM_CR_LVBIE       0x04
#define NABM_CR_FEIE        0x08
#define NABM_CR_IOCE        0x10

/* GLOB_CNT / GLOB_STA bits */
#define NABM_GLOB_CNT_GIE   0x00000001
#define NABM_GLOB_CNT_CR    0x00000002
#define NABM_GLOB_STA_POINT 0x00000100

/* -----------------------------------------------------------------------
 * BDL entry
 * ----------------------------------------------------------------------- */
#define AC97_BDL_SLOTS    32
#define AC97_BDL_USE      AC97_BDL_SLOTS
#define AC97_PERIOD_BYTES 2048
#define AC97_PERIOD_WORDS (AC97_PERIOD_BYTES / 2)

struct ac97_bdl_entry {
  uint32_t phys_addr;
  uint16_t n_samples;
  uint16_t ctl;
} __attribute__((packed));

#define AC97_BDL_CTL_IOC  0x8000
#define AC97_BDL_CTL_BUP  0x4000

/* -----------------------------------------------------------------------
 * Per-device state
 * ----------------------------------------------------------------------- */
#define AUDIO_INTEL_AC97_MAX 4

struct ac97_softc {
  uint16_t nambar;
  uint16_t nabm;
  uint8_t  irq;
  uint8_t  active;

  struct ac97_bdl_entry bdl[AC97_BDL_USE] __attribute__((aligned(8)));
  char bounce[AC97_BDL_USE][AC97_PERIOD_BYTES] __attribute__((aligned(4)));
  uint8_t lvi;

  uint32_t irq_count;
  uint32_t poll_count;
  uint32_t bcis_count;
  uint32_t lvbci_count;
  uint32_t fifoe_count;
  uint16_t last_sr;
};

static struct ac97_softc devs[AUDIO_INTEL_AC97_MAX];
static int count;

static inline uint8_t  nabm_inb(struct ac97_softc *sc, uint8_t r);
static inline uint16_t nabm_inw(struct ac97_softc *sc, uint8_t r);
static inline void     nabm_outb(struct ac97_softc *sc, uint8_t r, uint8_t v);
static inline void     nabm_outw(struct ac97_softc *sc, uint8_t r, uint16_t v);

int
audio_intel_ac97_debug_snapshot(uint32_t *irq_count,
                                uint32_t *poll_count,
                                uint32_t *bcis_count,
                                uint32_t *lvbci_count,
                                uint32_t *fifoe_count,
                                uint32_t *civ,
                                uint32_t *lvi,
                                uint32_t *picb,
                                uint32_t *cr,
                                uint32_t *sr)
{
  struct ac97_softc *sc;
  uint8_t chan;

  if(count <= 0)
    return -1;

  sc = &devs[0];
  if(!sc->active)
    return -1;

  chan = NABM_PCM_OUT_BASE;

  if(irq_count)
    *irq_count = sc->irq_count;
  if(poll_count)
    *poll_count = sc->poll_count;
  if(bcis_count)
    *bcis_count = sc->bcis_count;
  if(lvbci_count)
    *lvbci_count = sc->lvbci_count;
  if(fifoe_count)
    *fifoe_count = sc->fifoe_count;
  if(civ)
    *civ = nabm_inb(sc, chan + NABM_OFF_CIV);
  if(lvi)
    *lvi = nabm_inb(sc, chan + NABM_OFF_LVI);
  if(picb)
    *picb = nabm_inw(sc, chan + NABM_OFF_PICB);
  if(cr)
    *cr = nabm_inb(sc, chan + NABM_OFF_CR);
  if(sr)
    *sr = nabm_inw(sc, chan + NABM_OFF_SR);

  return 0;
}

static void
ac97_service_completion(struct ac97_softc *sc, uint16_t sr, int from_irq)
{
  uint8_t chan;
  uint8_t civ;
  uint8_t done_slot;

  chan = NABM_PCM_OUT_BASE;

  if(from_irq)
    sc->irq_count++;
  else
    sc->poll_count++;

  sc->last_sr = sr;
  if(sr & NABM_SR_BCIS)
    sc->bcis_count++;
  if(sr & NABM_SR_LVBCI)
    sc->lvbci_count++;
  if(sr & NABM_SR_FIFOE)
    sc->fifoe_count++;

  nabm_outw(sc, chan + NABM_OFF_SR, NABM_SR_LVBCI | NABM_SR_BCIS | NABM_SR_FIFOE);

  /* Refill the slot the hardware just finished. */
  civ = nabm_inb(sc, chan + NABM_OFF_CIV);
  done_slot = (uint8_t)((civ + AC97_BDL_USE - 1) % AC97_BDL_USE);
  audio_hw_period_advance(sc->bounce[done_slot], AC97_PERIOD_BYTES);

  sc->lvi = (uint8_t)((sc->lvi + 1) % AC97_BDL_USE);
  nabm_outb(sc, chan + NABM_OFF_LVI, sc->lvi);
}

void
audio_intel_ac97_poll(void)
{
  int i;
  uint8_t chan;

  chan = NABM_PCM_OUT_BASE;
  for(i = 0; i < count; i++){
    struct ac97_softc *sc = &devs[i];
    uint16_t sr;

    if(!sc->active)
      continue;
    sr = nabm_inw(sc, chan + NABM_OFF_SR);
    if(!(sr & (NABM_SR_BCIS | NABM_SR_LVBCI | NABM_SR_FIFOE)))
      continue;
    ac97_service_completion(sc, sr, 0);
  }
}

/* -----------------------------------------------------------------------
 * I/O helpers
 * ----------------------------------------------------------------------- */
static inline uint16_t nam_inw(struct ac97_softc *sc, uint8_t r)  { return inw(sc->nambar + r); }
static inline void     nam_outw(struct ac97_softc *sc, uint8_t r, uint16_t v) { outw(sc->nambar + r, v); }
static inline uint8_t  nabm_inb(struct ac97_softc *sc, uint8_t r)  { return inb(sc->nabm + r); }
static inline void     nabm_outb(struct ac97_softc *sc, uint8_t r, uint8_t v)  { outb(sc->nabm + r, v); }
static inline uint16_t nabm_inw(struct ac97_softc *sc, uint8_t r)  { return inw(sc->nabm + r); }
static inline void     nabm_outw(struct ac97_softc *sc, uint8_t r, uint16_t v) { outw(sc->nabm + r, v); }
static inline uint32_t nabm_inl(struct ac97_softc *sc, uint8_t r)  { return inl(sc->nabm + r); }
static inline void     nabm_outl(struct ac97_softc *sc, uint8_t r, uint32_t v) { outl(sc->nabm + r, v); }

/* -----------------------------------------------------------------------
 * Codec reset and init
 * ----------------------------------------------------------------------- */
static void
ac97_codec_reset(struct ac97_softc *sc)
{
  int i;

  nabm_outl(sc, NABM_GLOB_CNT, NABM_GLOB_CNT_CR);
  for(i = 0; i < 10000; i++){
    if(nabm_inl(sc, NABM_GLOB_STA) & NABM_GLOB_STA_POINT)
      break;
  }
  nam_outw(sc, AC97_RESET, 0);
  for(i = 0; i < 10000; i++) /* settling delay */ ;
}

static void
ac97_codec_init(struct ac97_softc *sc)
{
  uint16_t ext;

  nam_outw(sc, AC97_MASTER_VOL,  0x0000);
  nam_outw(sc, AC97_MONO_VOL,    0x0000);
  nam_outw(sc, AC97_PCM_OUT_VOL, 0x0000);

  ext = nam_inw(sc, AC97_EXT_AUDIO_ID);
  if(ext & 0x0001){
    uint16_t ctl = nam_inw(sc, AC97_EXT_AUDIO_CTL);
    nam_outw(sc, AC97_EXT_AUDIO_CTL, ctl | 0x0001);
    nam_outw(sc, AC97_PCM_FRONT_RATE, 48000);
  }
}

/* -----------------------------------------------------------------------
 * DMA channel setup
 * ----------------------------------------------------------------------- */
static void
ac97_pcmout_setup(struct ac97_softc *sc)
{
  int i;
  uint32_t gcnt;
  uint8_t chan = NABM_PCM_OUT_BASE;

  /* Reset PCM-Out channel */
  nabm_outb(sc, chan + NABM_OFF_CR, NABM_CR_RR);
  for(i = 0; i < 1000; i++){
    if(!(nabm_inb(sc, chan + NABM_OFF_CR) & NABM_CR_RR))
      break;
  }

  /* Fill bounce buffers with silence and build BDL */
  for(i = 0; i < AC97_BDL_USE; i++){
    memset(sc->bounce[i], 0, AC97_PERIOD_BYTES);
    sc->bdl[i].phys_addr = V2P(sc->bounce[i]);
    sc->bdl[i].n_samples = (uint16_t)AC97_PERIOD_WORDS;
    sc->bdl[i].ctl       = AC97_BDL_CTL_IOC | AC97_BDL_CTL_BUP;
  }

  nabm_outl(sc, chan + NABM_OFF_BDBAR, V2P(&sc->bdl[0]));

  sc->lvi = AC97_BDL_USE - 1;
  nabm_outb(sc, chan + NABM_OFF_LVI, sc->lvi);

  /* Clear status */
  nabm_outw(sc, chan + NABM_OFF_SR, NABM_SR_LVBCI | NABM_SR_BCIS | NABM_SR_FIFOE);

  /* Enable global AC97 interrupt forwarding. */
  gcnt = nabm_inl(sc, NABM_GLOB_CNT);
  nabm_outl(sc, NABM_GLOB_CNT, gcnt | NABM_GLOB_CNT_GIE);

  /* Run with completion interrupts enabled */
  nabm_outb(sc, chan + NABM_OFF_CR,
            NABM_CR_RPBM | NABM_CR_LVBIE | NABM_CR_IOCE | NABM_CR_FEIE);
}

/* -----------------------------------------------------------------------
 * Interrupt handler
 * ----------------------------------------------------------------------- */
static void
ac97_intr(int irq, void *arg)
{
  struct ac97_softc *sc = (struct ac97_softc *)arg;
  uint8_t chan = NABM_PCM_OUT_BASE;
  uint16_t sr;

  (void)irq;

  sr = nabm_inw(sc, chan + NABM_OFF_SR);
  if(!(sr & (NABM_SR_BCIS | NABM_SR_LVBCI | NABM_SR_FIFOE)))
    return;
  ac97_service_completion(sc, sr, 1);
}

/* -----------------------------------------------------------------------
 * Probe / attach
 * ----------------------------------------------------------------------- */
static const struct audio_pci_stub_match match_tbl[] = {
  { "intel-ac97", PCI_VENDOR_INTEL, AUDIO_PCI_ANY_DEVICE,
    PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_MULTIMEDIA_AUDIO,
    AUDIO_DEVF_CAN_PLAYBACK | AUDIO_DEVF_CAN_CAPTURE |
    AUDIO_DEVF_OSS_DSP_COMPAT | AUDIO_DEVF_OSS_MIXER_COMPAT,
    AUDIO_HW_PROFILE_AC97 },
};

void
audio_intel_ac97_init(void)
{
  int i;

  BOOTDBG("audio/intel-ac97: probe\n");

  for(i = 0; i < pci_device_count() && count < AUDIO_INTEL_AC97_MAX; i++){
    struct pci_dev *dev = pci_get_device(i);
    struct ac97_softc *sc;

    if(!audio_pci_stub_match_dev(&match_tbl[0], dev))
      continue;

    sc = &devs[count];
    memset(sc, 0, sizeof(*sc));

    pci_enable_io(dev);
    pci_set_master(dev);
    pci_enable_interrupts(dev);

    sc->nambar = (uint16_t)(pci_bar_base(dev, 0) & ~1U);
    sc->nabm   = (uint16_t)(pci_bar_base(dev, 1) & ~1U);
    sc->irq    = dev->irq_line;
    sc->active = 1;

    if(!(pci_bar_type(dev, 0) & PCI_BAR_IO) ||
       !(pci_bar_type(dev, 1) & PCI_BAR_IO)){
      BOOTDBG("audio/intel-ac97: BARs not I/O type, skipping\n");
      continue;
    }

    BOOTDBG("audio/intel-ac97: %d:%d.%d nambar=0x%x nabm=0x%x irq=%d\n",
            dev->bus, dev->slot, dev->func,
            sc->nambar, sc->nabm, sc->irq);

    if(audio_register_hw_device(dev->vendor_id, dev->device_id,
                                AUDIO_CARD_AUTO, 0,
                                AUDIO_DIR_PLAYBACK,
                                match_tbl[0].flags,
                                AUDIO_HW_PROFILE_AC97,
                                "intel-ac97") < 0){
      BOOTDBG("audio/intel-ac97: register failed\n");
      continue;
    }

    ac97_codec_reset(sc);
    ac97_codec_init(sc);
    ac97_pcmout_setup(sc);

    if(sc->irq > 0 && sc->irq < 24){
      irq_register(sc->irq, ac97_intr, sc, "intel-ac97");
      ioapicenable(sc->irq, 0);
    }

    count++;
  }
}

