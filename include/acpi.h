#ifndef AUXV6_ACPI_H
#define AUXV6_ACPI_H

#include "types.h"

struct acpi_rsdp {
  uchar signature[8];
  uchar checksum;
  uchar oemid[6];
  uchar revision;
  uint  rsdt_addr;
} __attribute__((packed));

struct acpi_rsdp2 {
  struct acpi_rsdp first;
  uint  length;
  unsigned long long xsdt_addr;
  uchar extended_checksum;
  uchar reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
  uchar signature[4];
  uint  length;
  uchar revision;
  uchar checksum;
  uchar oemid[6];
  uchar oem_table_id[8];
  uint  oem_revision;
  uint  creator_id;
  uint  creator_revision;
} __attribute__((packed));

struct acpi_gas {
  uchar address_space_id;
  uchar register_bit_width;
  uchar register_bit_offset;
  uchar access_size;
  unsigned long long address;
} __attribute__((packed));

struct acpi_madt {
  struct acpi_sdt_header hdr;
  uint lapic_addr;
  uint flags;
} __attribute__((packed));

struct acpi_hpet {
  struct acpi_sdt_header hdr;
  uint event_timer_block_id;
  struct acpi_gas base_address;
  uchar hpet_number;
  ushort min_tick;
  uchar page_protection;
} __attribute__((packed));

struct acpi_hpet_info {
  int present;
  unsigned long long address;
  uint event_timer_block_id;
  uchar hpet_number;
  ushort min_tick;
  uchar page_protection;
  uint comparator_count;
  int counter_64bit;
  int legacy_replacement;
  uint pci_vendor_id;
};

int   acpi_init(void);
void* acpi_find_table(const char sig[4]);
int   acpi_get_hpet_info(struct acpi_hpet_info *out);
int   acpi_get_interrupt_override(uchar source_irq, uint *gsi_out, ushort *flags_out);

#endif
