/*
 * 802.11 (Wi-Fi) PCI controller scaffold for auxv6.
 *
 * Scope of this tranche:
 * - Probe PCI class/subclass 0x02/0x80 and known vendor/device IDs for
 *   common 802.11 NIC families (Intel iwlwifi, Atheros ath5k/ath9k/ath10k,
 *   Broadcom b43/brcmsmac/brcmfmac, Ralink/MediaTek rt2x00, Realtek rtlwifi).
 * - Classify each candidate by family and advertised PHY capability set
 *   (802.11a/b/g/n/ac/ax) derived from PCI ID.
 * - Track controller phase state (init/scan/assoc/connected/degraded).
 * - Map BAR0, attempt interrupt visibility and bus-master enable.
 * - Expose discovered controllers and telemetry via /proc/wifi.
 *
 * This tranche does not implement any 802.11 MAC/MLME, firmware loading,
 * association state machines, or ifnet attachment.  Those are follow-on
 * tranches.
 */

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "pci.h"

/* --- limits ------------------------------------------------------------ */
#define WIFI_STUB_MAX        16

/* --- phase states ------------------------------------------------------- */
#define WIFI_PHASE_INIT      0   /* probed, hardware not started */
#define WIFI_PHASE_SCAN      1   /* background-scanning for BSSes */
#define WIFI_PHASE_ASSOC     2   /* MLME association in progress */
#define WIFI_PHASE_CONNECTED 3   /* associated and passing frames */
#define WIFI_PHASE_DEGRADED  4   /* init or firmware error */

/* --- PHY mode capability bitmap ---------------------------------------- */
#define WIFI_PHY_B    0x01   /* 802.11b  2.4 GHz DSSS/CCK up to 11 Mbps */
#define WIFI_PHY_A    0x02   /* 802.11a  5 GHz OFDM up to 54 Mbps */
#define WIFI_PHY_G    0x04   /* 802.11g  2.4 GHz OFDM up to 54 Mbps */
#define WIFI_PHY_N    0x08   /* 802.11n  HT 2.4/5 GHz up to ~600 Mbps */
#define WIFI_PHY_AC   0x10   /* 802.11ac VHT 5 GHz up to ~3.5 Gbps */
#define WIFI_PHY_AX   0x20   /* 802.11ax HE (Wi-Fi 6/6E) 2.4/5/6 GHz */

/* --- device families ---------------------------------------------------- */
#define WIFI_FAMILY_UNKNOWN  0
#define WIFI_FAMILY_INTEL    1   /* iwlwifi / ipw2x00 */
#define WIFI_FAMILY_ATHEROS  2   /* ath5k / ath9k / ath10k / ath11k */
#define WIFI_FAMILY_BROADCOM 3   /* b43 / brcmsmac / brcmfmac */
#define WIFI_FAMILY_RALINK   4   /* rt2x00 */
#define WIFI_FAMILY_MEDIATEK 5   /* mt76 */
#define WIFI_FAMILY_REALTEK  6   /* rtl818x / rtlwifi */

/* --- per-controller probe record --------------------------------------- */
struct wifi_probe {
  ushort  vendor_id;
  ushort  device_id;
  uchar   bus;
  uchar   slot;
  uchar   func;
  uchar   irq_line;
  uint    bar0;
  uint    bar0_size;
  uchar   family;
  uchar   phy_modes;    /* WIFI_PHY_* bitmap */
  uchar   phase;
  uchar   bar_mapped;
  uchar   busmaster_ok;
  uchar   irq_registered;
  uchar   init_failures;
  uint    attach_ticks;
  uint    probe_attempts;
  uint    probe_successes;
  uint    probe_failures;
  volatile uint *regs;
};

/* --- PCI ID table entry ------------------------------------------------- */
struct wifi_pci_id {
  ushort vendor_id;
  ushort device_id;
  uchar  family;
  uchar  phy_modes;
  const char *model;
};

/*
 * Known-good PCI ID table.  Entries are kept in vendor order; within each
 * vendor they are kept in ascending device ID order.
 *
 * PHY mode bitmaps are based on published hardware capability; later tranches
 * can refine by reading firmware capability advertisements.
 */
static const struct wifi_pci_id wifi_id_table[] = {
  /* --- Intel (vendor 0x8086) ------------------------------------------ */
  /* ipw2100 */
  { 0x8086, 0x1043, WIFI_FAMILY_INTEL, WIFI_PHY_B,                   "Intel PRO/Wireless 2100" },
  /* ipw2200 */
  { 0x8086, 0x4220, WIFI_FAMILY_INTEL, WIFI_PHY_B|WIFI_PHY_G,        "Intel PRO/Wireless 2200BG" },
  { 0x8086, 0x4223, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G, "Intel PRO/Wireless 2915ABG" },
  { 0x8086, 0x4224, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G, "Intel PRO/Wireless 2915ABG" },
  /* iwl3945 */
  { 0x8086, 0x4222, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G, "Intel PRO/Wireless 3945ABG" },
  { 0x8086, 0x4227, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G, "Intel PRO/Wireless 3945ABG" },
  /* iwl4965 */
  { 0x8086, 0x4229, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Intel WiFi Link 4965AGN" },
  /* iwlagn (5xxx/6xxx) */
  { 0x8086, 0x4232, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Intel WiFi Link 5100" },
  { 0x8086, 0x4235, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Intel WiFi Link 5300" },
  { 0x8086, 0x4236, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Intel WiFi Link 5300" },
  { 0x8086, 0x0083, WIFI_FAMILY_INTEL, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N,             "Intel Centrino Wireless-N 1000" },
  { 0x8086, 0x0084, WIFI_FAMILY_INTEL, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N,             "Intel Centrino Wireless-N 1000" },
  { 0x8086, 0x0085, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Intel Centrino Advanced-N 6205" },
  { 0x8086, 0x0087, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Intel Centrino Advanced-N + WiMAX 6250" },
  { 0x8086, 0x0887, WIFI_FAMILY_INTEL, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N,             "Intel Centrino Wireless-N 2230" },
  { 0x8086, 0x0888, WIFI_FAMILY_INTEL, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N,             "Intel Centrino Wireless-N 2230" },
  { 0x8086, 0x088e, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Intel Centrino Advanced-N 6235" },
  { 0x8086, 0x088f, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Intel Centrino Advanced-N 6235" },
  /* iwlwifi (7xxx/8xxx) */
  { 0x8086, 0x08b1, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Intel Wireless 7260" },
  { 0x8086, 0x08b2, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Intel Wireless 7260" },
  { 0x8086, 0x08b3, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N,             "Intel Wireless 3160" },
  { 0x8086, 0x095a, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Intel Wireless 7265" },
  { 0x8086, 0x095b, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Intel Wireless 7265" },
  { 0x8086, 0x3165, WIFI_FAMILY_INTEL, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC,             "Intel Dual Band Wireless-AC 3165" },
  { 0x8086, 0x24fb, WIFI_FAMILY_INTEL, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC,             "Intel Dual Band Wireless-AC 3168" },
  { 0x8086, 0x24f3, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Intel Wireless 8260" },
  { 0x8086, 0x24f4, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Intel Wireless 8260" },
  { 0x8086, 0x24fd, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Intel Wireless 8265" },
  { 0x8086, 0x2526, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Intel Wireless 9260" },
  /* iwlwifi (Wi-Fi 6/6E AX-class) */
  { 0x8086, 0x02f0, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC|WIFI_PHY_AX, "Intel Wi-Fi 6 AX201" },
  { 0x8086, 0x2723, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC|WIFI_PHY_AX, "Intel Wi-Fi 6 AX200" },
  { 0x8086, 0x2725, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC|WIFI_PHY_AX, "Intel Wi-Fi 6E AX210" },
  { 0x8086, 0x51f0, WIFI_FAMILY_INTEL, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC|WIFI_PHY_AX, "Intel Wi-Fi 6E AX211" },
  /* --- Atheros / Qualcomm Atheros (vendor 0x168c) --------------------- */
  { 0x168c, 0x0013, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B,                  "Atheros AR5212/5213 (ath5k)" },
  { 0x168c, 0x0019, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G,       "Atheros AR5213A (ath5k)" },
  { 0x168c, 0x001a, WIFI_FAMILY_ATHEROS, WIFI_PHY_B|WIFI_PHY_G,                  "Atheros AR2413/AR5413 (ath5k)" },
  { 0x168c, 0x001c, WIFI_FAMILY_ATHEROS, WIFI_PHY_B|WIFI_PHY_G,                  "Atheros AR5424 (ath5k)" },
  { 0x168c, 0x0023, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Atheros AR5416 (ath9k)" },
  { 0x168c, 0x0029, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Atheros AR9280 (ath9k)" },
  { 0x168c, 0x002a, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Atheros AR928x (ath9k)" },
  { 0x168c, 0x002b, WIFI_FAMILY_ATHEROS, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N,            "Atheros AR9285 (ath9k)" },
  { 0x168c, 0x002e, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Atheros AR9287 (ath9k)" },
  { 0x168c, 0x0030, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Atheros AR9380 (ath9k)" },
  { 0x168c, 0x0032, WIFI_FAMILY_ATHEROS, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N,            "Atheros AR9485 (ath9k)" },
  { 0x168c, 0x0033, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Atheros AR9580 (ath9k)" },
  { 0x168c, 0x0034, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Qualcomm Atheros AR9462 (ath9k)" },
  { 0x168c, 0x0036, WIFI_FAMILY_ATHEROS, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N,            "Qualcomm Atheros AR9565 (ath9k)" },
  { 0x168c, 0x003c, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Qualcomm Atheros QCA986x (ath10k)" },
  { 0x168c, 0x003e, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Qualcomm Atheros QCA6174 (ath10k)" },
  { 0x168c, 0x0042, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Qualcomm Atheros QCA9377 (ath10k)" },
  { 0x168c, 0x0046, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Qualcomm Atheros QCA6390 (ath11k)" },
  { 0x168c, 0x1101, WIFI_FAMILY_ATHEROS, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC|WIFI_PHY_AX, "Qualcomm WCN6855 (ath11k)" },
  /* --- Broadcom (vendor 0x14e4) --------------------------------------- */
  { 0x14e4, 0x4307, WIFI_FAMILY_BROADCOM, WIFI_PHY_B|WIFI_PHY_G,                  "Broadcom BCM4306 (b43)" },
  { 0x14e4, 0x4311, WIFI_FAMILY_BROADCOM, WIFI_PHY_B|WIFI_PHY_G,                  "Broadcom BCM4311 (b43)" },
  { 0x14e4, 0x4315, WIFI_FAMILY_BROADCOM, WIFI_PHY_B|WIFI_PHY_G,                  "Broadcom BCM4312 (b43)" },
  { 0x14e4, 0x4318, WIFI_FAMILY_BROADCOM, WIFI_PHY_B|WIFI_PHY_G,                  "Broadcom BCM4318 (b43)" },
  { 0x14e4, 0x4324, WIFI_FAMILY_BROADCOM, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G,       "Broadcom BCM4321 (b43)" },
  { 0x14e4, 0x432b, WIFI_FAMILY_BROADCOM, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G,       "Broadcom BCM4322 (b43)" },
  { 0x14e4, 0x4353, WIFI_FAMILY_BROADCOM, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N,       "Broadcom BCM43224/43225 (brcmsmac)" },
  { 0x14e4, 0x43a0, WIFI_FAMILY_BROADCOM, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Broadcom BCM4360 (brcmfmac)" },
  { 0x14e4, 0x43b1, WIFI_FAMILY_BROADCOM, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Broadcom BCM4352 (brcmfmac)" },
  { 0x14e4, 0x43ba, WIFI_FAMILY_BROADCOM, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Broadcom BCM43602 (brcmfmac)" },
  { 0x14e4, 0x43dc, WIFI_FAMILY_BROADCOM, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC|WIFI_PHY_AX, "Broadcom BCM4375 (brcmfmac)" },
  /* --- Ralink Technology (vendor 0x1814) ------------------------------ */
  { 0x1814, 0x0201, WIFI_FAMILY_RALINK, WIFI_PHY_B|WIFI_PHY_G,                  "Ralink RT2500 (rt2x00)" },
  { 0x1814, 0x0301, WIFI_FAMILY_RALINK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G,       "Ralink RT2561 (rt61pci)" },
  { 0x1814, 0x0401, WIFI_FAMILY_RALINK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G,       "Ralink RT2661 (rt61pci)" },
  { 0x1814, 0x0601, WIFI_FAMILY_RALINK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Ralink RT2860 (rt2x00)" },
  { 0x1814, 0x3090, WIFI_FAMILY_RALINK, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N,            "Ralink RT3090 (rt2x00)" },
  { 0x1814, 0x3290, WIFI_FAMILY_RALINK, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N,            "Ralink RT3290 (rt2x00)" },
  /* --- MediaTek (vendor 0x14c3) --------------------------------------- */
  { 0x14c3, 0x7603, WIFI_FAMILY_MEDIATEK, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N,            "MediaTek MT7603E (mt76)" },
  { 0x14c3, 0x7612, WIFI_FAMILY_MEDIATEK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "MediaTek MT7612E (mt76)" },
  { 0x14c3, 0x7615, WIFI_FAMILY_MEDIATEK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "MediaTek MT7615 (mt76)" },
  { 0x14c3, 0x7663, WIFI_FAMILY_MEDIATEK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "MediaTek MT7663 (mt76)" },
  { 0x14c3, 0x7921, WIFI_FAMILY_MEDIATEK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "MediaTek MT7921E (mt76)" },
  { 0x14c3, 0x7922, WIFI_FAMILY_MEDIATEK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC|WIFI_PHY_AX, "MediaTek MT7922 (mt76)" },
  /* --- Realtek (vendor 0x10ec) ---------------------------------------- */
  { 0x10ec, 0x8180, WIFI_FAMILY_REALTEK, WIFI_PHY_B,                  "Realtek RTL8180 (rtl818x)" },
  { 0x10ec, 0x8185, WIFI_FAMILY_REALTEK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G, "Realtek RTL8185 (rtl818x)" },
  { 0x10ec, 0x8176, WIFI_FAMILY_REALTEK, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Realtek RTL8188CE (rtl8188ee)" },
  { 0x10ec, 0x8179, WIFI_FAMILY_REALTEK, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Realtek RTL8188EE (rtlwifi)" },
  { 0x10ec, 0x8177, WIFI_FAMILY_REALTEK, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Realtek RTL8192CE (rtl8192ce)" },
  { 0x10ec, 0x8192, WIFI_FAMILY_REALTEK, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Realtek RTL8192SE (rtl8192se)" },
  { 0x10ec, 0x8193, WIFI_FAMILY_REALTEK, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Realtek RTL8192DE (rtl8192de)" },
  { 0x10ec, 0x818b, WIFI_FAMILY_REALTEK, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Realtek RTL8192EE (rtlwifi)" },
  { 0x10ec, 0x8723, WIFI_FAMILY_REALTEK, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Realtek RTL8723AE (rtlwifi)" },
  { 0x10ec, 0xb723, WIFI_FAMILY_REALTEK, WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N, "Realtek RTL8723BE (rtlwifi)" },
  { 0x10ec, 0x8821, WIFI_FAMILY_REALTEK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Realtek RTL8821AE (rtlwifi)" },
  { 0x10ec, 0xb822, WIFI_FAMILY_REALTEK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Realtek RTL8822BE (rtlwifi)" },
  { 0x10ec, 0xc822, WIFI_FAMILY_REALTEK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC, "Realtek RTL8822CE (rtlwifi)" },
  { 0x10ec, 0xc852, WIFI_FAMILY_REALTEK, WIFI_PHY_A|WIFI_PHY_B|WIFI_PHY_G|WIFI_PHY_N|WIFI_PHY_AC|WIFI_PHY_AX, "Realtek RTL8852AE (rtlwifi)" },
};

#define WIFI_ID_TABLE_LEN (sizeof(wifi_id_table) / sizeof(wifi_id_table[0]))

/* --- module state ------------------------------------------------------- */
static struct spinlock wifi_lock;
static int wifi_lock_ready;
static struct wifi_probe wifi_probes[WIFI_STUB_MAX];
static uint wifi_probe_count;

/* --- helpers ------------------------------------------------------------ */

static const struct wifi_pci_id *
wifi_lookup_id(ushort vendor, ushort device)
{
  uint i;

  for(i = 0; i < WIFI_ID_TABLE_LEN; i++){
    if(wifi_id_table[i].vendor_id == vendor &&
       wifi_id_table[i].device_id == device)
      return &wifi_id_table[i];
  }
  return 0;
}

/*
 * PCI class match: 802.11 wireless NICs use class=0x02 subclass=0x80.
 * Some older parts (Atheros early, certain Ralink) ship as 0x02/0x00.
 * Accept both; vendor/device ID is the authoritative match when present.
 */
static int
wifi_class_match(struct pci_dev *dev)
{
  if(dev->class_code != 0x02)
    return 0;
  return (dev->subclass == 0x80 || dev->subclass == 0x00);
}

static int
wifi_is_match(struct pci_dev *dev)
{
  if(!dev)
    return 0;
  /* Explicit ID hit takes precedence over class-only match */
  if(wifi_lookup_id(dev->vendor_id, dev->device_id))
    return 1;
  /* Fall through to class-based discovery for unknown parts */
  if(wifi_class_match(dev))
    return 1;
  return 0;
}

static const char *
wifi_family_name(uchar family)
{
  switch(family){
  case WIFI_FAMILY_INTEL:    return "intel";
  case WIFI_FAMILY_ATHEROS:  return "atheros";
  case WIFI_FAMILY_BROADCOM: return "broadcom";
  case WIFI_FAMILY_RALINK:   return "ralink";
  case WIFI_FAMILY_MEDIATEK: return "mediatek";
  case WIFI_FAMILY_REALTEK:  return "realtek";
  default:                   return "unknown";
  }
}

static const char *
wifi_phase_name(uchar phase)
{
  switch(phase){
  case WIFI_PHASE_INIT:      return "init";
  case WIFI_PHASE_SCAN:      return "scan";
  case WIFI_PHASE_ASSOC:     return "assoc";
  case WIFI_PHASE_CONNECTED: return "connected";
  case WIFI_PHASE_DEGRADED:  return "degraded";
  default:                   return "unknown";
  }
}

/* Append PHY mode letters (e.g. "bgn", "abgnac") to buf[off..off+max). */
static uint
wifi_phy_str(char *buf, uint max, uint off, uchar modes)
{
  static const struct { uchar bit; char ch; } phy_chars[] = {
    { WIFI_PHY_B,  'b' },
    { WIFI_PHY_A,  'a' },
    { WIFI_PHY_G,  'g' },
    { WIFI_PHY_N,  'n' },
    { WIFI_PHY_AC, 'c' },  /* write 'c' for 'ac' */
    { WIFI_PHY_AX, 'x' },  /* write 'x' for 'ax' */
  };
  uint i;

  for(i = 0; i < sizeof(phy_chars)/sizeof(phy_chars[0]); i++){
    if((modes & phy_chars[i].bit) && off < max)
      buf[off++] = phy_chars[i].ch;
  }
  return off;
}

/* --- procfs output helpers ---------------------------------------------- */

static int
wifi_buf_putc(char *buf, uint max, uint *len, char c)
{
  if(*len >= max)
    return -1;
  buf[(*len)++] = c;
  return 0;
}

static int
wifi_buf_puts(char *buf, uint max, uint *len, const char *s)
{
  if(!s)
    return 0;
  while(*s){
    if(wifi_buf_putc(buf, max, len, *s++) < 0)
      return -1;
  }
  return 0;
}

static int
wifi_buf_putu(char *buf, uint max, uint *len, uint v)
{
  char tmp[12];
  uint n = 0;

  do {
    tmp[n++] = '0' + (v % 10);
    v /= 10;
  } while(v);
  while(n--){
    if(wifi_buf_putc(buf, max, len, tmp[n]) < 0)
      return -1;
  }
  return 0;
}

static int
wifi_buf_puthex16(char *buf, uint max, uint *len, ushort v)
{
  static const char hex[] = "0123456789abcdef";
  int i;

  for(i = 3; i >= 0; i--){
    if(wifi_buf_putc(buf, max, len, hex[(v >> (i * 4)) & 0xf]) < 0)
      return -1;
  }
  return 0;
}

/* --- PCI attach --------------------------------------------------------- */

static int
wifi_attach_probe(struct pci_dev *dev)
{
  struct wifi_probe *sc;
  const struct wifi_pci_id *id;
  uint bar0, bar0_size;

  acquire(&wifi_lock);
  if(wifi_probe_count >= WIFI_STUB_MAX){
    release(&wifi_lock);
    return -1;
  }
  sc = &wifi_probes[wifi_probe_count++];
  release(&wifi_lock);

  sc->vendor_id   = dev->vendor_id;
  sc->device_id   = dev->device_id;
  sc->bus         = dev->bus;
  sc->slot        = dev->slot;
  sc->func        = dev->func;
  sc->irq_line    = dev->irq_line;
  sc->phase       = WIFI_PHASE_INIT;
  sc->probe_attempts = 1;

  /* Classify by ID table */
  id = wifi_lookup_id(dev->vendor_id, dev->device_id);
  if(id){
    sc->family    = id->family;
    sc->phy_modes = id->phy_modes;
  } else {
    sc->family    = WIFI_FAMILY_UNKNOWN;
    sc->phy_modes = 0;
  }

  /* BAR0 */
  bar0 = pci_bar_base(dev, 0);
  bar0_size = pci_bar_size(dev, 0);
  sc->bar0      = bar0;
  sc->bar0_size = bar0_size;

  if(bar0 && bar0_size >= 64){
    void *mapped = pci_map_bar(dev, 0);
    if(mapped){
      sc->regs      = (volatile uint *)mapped;
      sc->bar_mapped = 1;
    }
  }

  /* Enable bus mastering so DMA can proceed when a real driver attaches */
  pci_set_master(dev);
  pci_enable_mem(dev);
  sc->busmaster_ok = 1;

  sc->probe_successes = 1;
  cprintf("wifi: %s %s [%x:%x] at %d:%d.%d irq=%d bar0=0x%x phymodes=%d\n",
    wifi_family_name(sc->family),
    (id ? id->model : "unknown"),
    (uint)dev->vendor_id, (uint)dev->device_id,
    (int)dev->bus, (int)dev->slot, (int)dev->func,
    (int)dev->irq_line,
    bar0,
    (int)sc->phy_modes);

  return 0;
}

/* --- /proc/wifi dump ---------------------------------------------------- */

int
wifi_procfs_dump(char *buf, uint max)
{
  struct wifi_probe snap[WIFI_STUB_MAX];
  uint count;
  uint len = 0;
  uint i;

  acquire(&wifi_lock);
  count = wifi_probe_count;
  if(count > WIFI_STUB_MAX)
    count = WIFI_STUB_MAX;
  for(i = 0; i < count; i++)
    snap[i] = wifi_probes[i];
  release(&wifi_lock);

  /* Header */
  if(wifi_buf_puts(buf, max, &len, "# 802.11 Wi-Fi controllers\n") < 0) return -1;
  if(wifi_buf_puts(buf, max, &len, "# vendor device bus:slot.fn family phy phase bar0\n") < 0) return -1;

  for(i = 0; i < count; i++){
    struct wifi_probe *p = &snap[i];

    if(wifi_buf_puts(buf, max, &len, "dev ") < 0) return -1;
    if(wifi_buf_puthex16(buf, max, &len, p->vendor_id) < 0) return -1;
    if(wifi_buf_putc(buf, max, &len, ':') < 0) return -1;
    if(wifi_buf_puthex16(buf, max, &len, p->device_id) < 0) return -1;

    if(wifi_buf_puts(buf, max, &len, " bus=") < 0) return -1;
    if(wifi_buf_putu(buf, max, &len, p->bus) < 0) return -1;
    if(wifi_buf_putc(buf, max, &len, ':') < 0) return -1;
    if(wifi_buf_putu(buf, max, &len, p->slot) < 0) return -1;
    if(wifi_buf_putc(buf, max, &len, '.') < 0) return -1;
    if(wifi_buf_putu(buf, max, &len, p->func) < 0) return -1;

    if(wifi_buf_puts(buf, max, &len, " family=") < 0) return -1;
    if(wifi_buf_puts(buf, max, &len, wifi_family_name(p->family)) < 0) return -1;

    if(wifi_buf_puts(buf, max, &len, " phy=") < 0) return -1;
    if(p->phy_modes){
      len = wifi_phy_str(buf, max, len, p->phy_modes);
    } else {
      if(wifi_buf_putc(buf, max, &len, '?') < 0) return -1;
    }

    if(wifi_buf_puts(buf, max, &len, " phase=") < 0) return -1;
    if(wifi_buf_puts(buf, max, &len, wifi_phase_name(p->phase)) < 0) return -1;

    if(wifi_buf_puts(buf, max, &len, " bar0=0x") < 0) return -1;
    /* emit bar0 as hex uint */
    {
      static const char hx[] = "0123456789abcdef";
      int shift;
      for(shift = 28; shift >= 0; shift -= 4){
        if(wifi_buf_putc(buf, max, &len, hx[(p->bar0 >> shift) & 0xf]) < 0)
          return -1;
      }
    }

    if(wifi_buf_puts(buf, max, &len, " irq=") < 0) return -1;
    if(wifi_buf_putu(buf, max, &len, p->irq_line) < 0) return -1;

    if(p->busmaster_ok){
      if(wifi_buf_puts(buf, max, &len, " busmaster") < 0) return -1;
    }
    if(p->bar_mapped){
      if(wifi_buf_puts(buf, max, &len, " mapped") < 0) return -1;
    }
    if(wifi_buf_putc(buf, max, &len, '\n') < 0) return -1;
  }

  if(wifi_buf_puts(buf, max, &len, "summary total=") < 0) return -1;
  if(wifi_buf_putu(buf, max, &len, count) < 0) return -1;
  if(wifi_buf_puts(buf, max, &len, " mac=0 assoc=0 connected=0\n") < 0) return -1;

  return (int)len;
}

/* --- init --------------------------------------------------------------- */

void
wifi_init(void)
{
  int i;
  uint found;

  if(!wifi_lock_ready){
    initlock(&wifi_lock, "wifi");
    lockdep_set_rank(&wifi_lock, LOCK_RANK_DEFAULT, "wifi");
    wifi_lock_ready = 1;
  }

  acquire(&wifi_lock);
  wifi_probe_count = 0;
  release(&wifi_lock);

  found = 0;
  BOOTDBG("wifi: probing 802.11 controllers\n");
  for(i = 0; i < pci_device_count(); i++){
    struct pci_dev *dev = pci_get_device(i);
    if(!wifi_is_match(dev))
      continue;
    if(wifi_attach_probe(dev) == 0)
      found++;
  }
  BOOTDBG("wifi: discovered %d 802.11 controller(s)\n", found);
}
