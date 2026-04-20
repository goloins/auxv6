#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "acpi.h"

#define ACPI_ROOT_RSDT 1
#define ACPI_ROOT_XSDT 2
#define ACPI_MAX_ISO   16
#define ACPI_MAX_SDT_LEN (1024U * 1024U)

struct acpi_madt_hdr {
  uchar type;
  uchar length;
} __attribute__((packed));

struct acpi_madt_iso {
  uchar type;
  uchar length;
  uchar bus;
  uchar source;
  uint  gsi;
  ushort flags;
} __attribute__((packed));

struct acpi_iso_override {
  uchar source;
  uint gsi;
  ushort flags;
};

static struct acpi_rsdp2 *acpi_rsdp_ptr;
static struct acpi_sdt_header *acpi_root_sdt;
static int acpi_root_kind;
static struct acpi_madt *acpi_madt_ptr;
static struct acpi_hpet *acpi_hpet_ptr;
static struct acpi_hpet_info acpi_hpet_cached;
static struct acpi_iso_override acpi_iso[ACPI_MAX_ISO];
static int acpi_iso_count;
static int acpi_ready;

static uchar
acpi_checksum(const uchar *p, uint len)
{
  uint i;
  uchar sum;

  sum = 0;
  for(i = 0; i < len; i++)
    sum = (uchar)(sum + p[i]);
  return sum;
}

static void*
acpi_pa_to_ptr(unsigned long long pa64, uint len)
{
  uint pa;

  if((pa64 >> 32) != 0)
    return 0;
  pa = (uint)pa64;

  if(pa >= DEVSPACE)
    return (void*)pa;

  if(pa >= PHYSTOP)
    return 0;
  if(len > PHYSTOP - pa)
    return 0;

  return P2V(pa);
}

static struct acpi_sdt_header*
acpi_map_sdt(unsigned long long pa64)
{
  struct acpi_sdt_header *hdr;

  hdr = (struct acpi_sdt_header*)acpi_pa_to_ptr(pa64, sizeof(*hdr));
  if(hdr == 0)
    return 0;
  if(hdr->length < sizeof(*hdr) || hdr->length > ACPI_MAX_SDT_LEN)
    return 0;
  hdr = (struct acpi_sdt_header*)acpi_pa_to_ptr(pa64, hdr->length);
  if(hdr == 0)
    return 0;
  if(acpi_checksum((const uchar*)hdr, hdr->length) != 0)
    return 0;
  return hdr;
}

static struct acpi_rsdp2*
acpi_scan_rsdp_range(uint pa, uint len)
{
  uchar *p;
  uchar *e;
  struct acpi_rsdp *rsdp1;
  struct acpi_rsdp2 *rsdp2;

  p = (uchar*)acpi_pa_to_ptr(pa, len);
  if(p == 0)
    return 0;
  e = p + len;

  for(; p + sizeof(struct acpi_rsdp) <= e; p += 16){
    if(memcmp(p, "RSD PTR ", 8) != 0)
      continue;

    rsdp1 = (struct acpi_rsdp*)p;
    if(acpi_checksum((const uchar*)rsdp1, sizeof(*rsdp1)) != 0)
      continue;

    rsdp2 = (struct acpi_rsdp2*)p;
    if(rsdp1->revision >= 2){
      if(rsdp2->length < sizeof(*rsdp2))
        continue;
      if(acpi_checksum((const uchar*)rsdp2, rsdp2->length) != 0)
        continue;
    }
    return rsdp2;
  }

  return 0;
}

static struct acpi_rsdp2*
acpi_find_rsdp(void)
{
  uchar *bda;
  uint ebda;
  uint base_mem;
  struct acpi_rsdp2 *rsdp;

  bda = (uchar*)P2V(0x400);
  ebda = (uint)(((uint)bda[0x0F] << 8) | bda[0x0E]) << 4;
  if(ebda != 0){
    rsdp = acpi_scan_rsdp_range(ebda, 1024);
    if(rsdp)
      return rsdp;
  }

  base_mem = (uint)(((uint)bda[0x14] << 8) | bda[0x13]) * 1024U;
  if(base_mem >= 1024U){
    rsdp = acpi_scan_rsdp_range(base_mem - 1024U, 1024);
    if(rsdp)
      return rsdp;
  }

  return acpi_scan_rsdp_range(0xE0000, 0x20000);
}

static int
acpi_root_count(void)
{
  uint payload;
  uint entsz;

  if(acpi_root_sdt == 0)
    return 0;
  if(acpi_root_sdt->length < sizeof(*acpi_root_sdt))
    return 0;

  payload = acpi_root_sdt->length - sizeof(*acpi_root_sdt);
  entsz = (acpi_root_kind == ACPI_ROOT_XSDT) ? 8U : 4U;
  return (int)(payload / entsz);
}

static struct acpi_sdt_header*
acpi_root_entry(int idx)
{
  uchar *base;
  int count;

  if(acpi_root_sdt == 0)
    return 0;

  count = acpi_root_count();
  if(idx < 0 || idx >= count)
    return 0;

  base = (uchar*)acpi_root_sdt + sizeof(*acpi_root_sdt);
  if(acpi_root_kind == ACPI_ROOT_XSDT){
    unsigned long long *entry64;
    entry64 = (unsigned long long*)base;
    return acpi_map_sdt(entry64[idx]);
  } else {
    uint *entry32;
    entry32 = (uint*)base;
    return acpi_map_sdt((unsigned long long)entry32[idx]);
  }
}

static void
acpi_parse_madt(void)
{
  uchar *p;
  uchar *e;
  struct acpi_madt_hdr *h;
  int lapic_count;
  int ioapic_count;

  acpi_madt_ptr = (struct acpi_madt*)acpi_find_table("APIC");
  if(acpi_madt_ptr == 0)
    return;

  lapic_count = 0;
  ioapic_count = 0;
  acpi_iso_count = 0;

  p = (uchar*)acpi_madt_ptr + sizeof(*acpi_madt_ptr);
  e = (uchar*)acpi_madt_ptr + acpi_madt_ptr->hdr.length;
  while(p + sizeof(struct acpi_madt_hdr) <= e){
    h = (struct acpi_madt_hdr*)p;
    if(h->length < sizeof(*h) || p + h->length > e)
      break;

    switch(h->type){
    case 0:
      lapic_count++;
      break;
    case 1:
      ioapic_count++;
      break;
    case 2:
      if(h->length >= sizeof(struct acpi_madt_iso) && acpi_iso_count < ACPI_MAX_ISO){
        struct acpi_madt_iso *iso;
        iso = (struct acpi_madt_iso*)p;
        acpi_iso[acpi_iso_count].source = iso->source;
        acpi_iso[acpi_iso_count].gsi = iso->gsi;
        acpi_iso[acpi_iso_count].flags = iso->flags;
        acpi_iso_count++;
      }
      break;
    }

    p += h->length;
  }

  BOOTDBG("acpi: MADT lapic=%d ioapic=%d iso=%d lapicaddr=%x\n",
          lapic_count, ioapic_count, acpi_iso_count, acpi_madt_ptr->lapic_addr);
}

static void
acpi_parse_hpet(void)
{
  uint id;

  memset(&acpi_hpet_cached, 0, sizeof(acpi_hpet_cached));
  acpi_hpet_ptr = (struct acpi_hpet*)acpi_find_table("HPET");
  if(acpi_hpet_ptr == 0)
    return;

  id = acpi_hpet_ptr->event_timer_block_id;
  acpi_hpet_cached.present = 1;
  acpi_hpet_cached.address = acpi_hpet_ptr->base_address.address;
  acpi_hpet_cached.event_timer_block_id = id;
  acpi_hpet_cached.hpet_number = acpi_hpet_ptr->hpet_number;
  acpi_hpet_cached.min_tick = acpi_hpet_ptr->min_tick;
  acpi_hpet_cached.page_protection = acpi_hpet_ptr->page_protection;
  acpi_hpet_cached.comparator_count = ((id >> 8) & 0x1f) + 1;
  acpi_hpet_cached.counter_64bit = (id & (1U << 13)) ? 1 : 0;
  acpi_hpet_cached.legacy_replacement = (id & (1U << 15)) ? 1 : 0;
  acpi_hpet_cached.pci_vendor_id = (id >> 16) & 0xffff;

  BOOTDBG("acpi: HPET id=%x base=%x timers=%d 64bit=%d legacy=%d min=%d\n",
          id,
          (uint)acpi_hpet_cached.address,
          acpi_hpet_cached.comparator_count,
          acpi_hpet_cached.counter_64bit,
          acpi_hpet_cached.legacy_replacement,
          acpi_hpet_cached.min_tick);
}

int
acpi_init(void)
{
  if(acpi_ready)
    return 0;

  acpi_rsdp_ptr = acpi_find_rsdp();
  if(acpi_rsdp_ptr == 0){
    BOOTDBG("acpi: RSDP not found\n");
    return -1;
  }

  if(acpi_rsdp_ptr->first.revision >= 2 && acpi_rsdp_ptr->xsdt_addr != 0){
    acpi_root_sdt = acpi_map_sdt(acpi_rsdp_ptr->xsdt_addr);
    if(acpi_root_sdt)
      acpi_root_kind = ACPI_ROOT_XSDT;
  }

  if(acpi_root_sdt == 0 && acpi_rsdp_ptr->first.rsdt_addr != 0){
    acpi_root_sdt = acpi_map_sdt((unsigned long long)acpi_rsdp_ptr->first.rsdt_addr);
    if(acpi_root_sdt)
      acpi_root_kind = ACPI_ROOT_RSDT;
  }

  if(acpi_root_sdt == 0){
    BOOTDBG("acpi: root table invalid\n");
    return -1;
  }

  acpi_ready = 1;
  BOOTDBG("acpi: %s entries=%d rev=%d\n",
          acpi_root_kind == ACPI_ROOT_XSDT ? "XSDT" : "RSDT",
          acpi_root_count(),
          acpi_rsdp_ptr->first.revision);

  acpi_parse_madt();
  acpi_parse_hpet();
  return 0;
}

void*
acpi_find_table(const char sig[4])
{
  int i;
  int n;
  struct acpi_sdt_header *hdr;

  if(!acpi_ready || sig == 0)
    return 0;

  n = acpi_root_count();
  for(i = 0; i < n; i++){
    hdr = acpi_root_entry(i);
    if(hdr && memcmp(hdr->signature, sig, 4) == 0)
      return hdr;
  }

  return 0;
}

int
acpi_get_hpet_info(struct acpi_hpet_info *out)
{
  if(out == 0 || !acpi_hpet_cached.present)
    return -1;
  *out = acpi_hpet_cached;
  return 0;
}

int
acpi_get_interrupt_override(uchar source_irq, uint *gsi_out, ushort *flags_out)
{
  int i;

  for(i = 0; i < acpi_iso_count; i++){
    if(acpi_iso[i].source != source_irq)
      continue;
    if(gsi_out)
      *gsi_out = acpi_iso[i].gsi;
    if(flags_out)
      *flags_out = acpi_iso[i].flags;
    return 1;
  }

  if(gsi_out)
    *gsi_out = source_irq;
  if(flags_out)
    *flags_out = 0;
  return 0;
}
