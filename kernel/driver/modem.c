/*
 * PCI modem discovery scaffold for auxv6.
 *
 * Scope of this tranche:
 * - Detect common modem-class devices and known vendor families.
 * - Provide per-vendor init/probe hooks for future driver expansion.
 * - Keep behavior strictly probe-only (no data path yet).
 *
 * References for structure and family coverage:
 * - Linux and BSD public driver trees for softmodem/controller modem IDs.
 */

#include "types.h"
#include "defs.h"

void
modem_init(void)
{
  BOOTDBG("modem: probing PCI modem families (stub tranche)\n");
  conexant_hsf_init();
  agere_lt_init();
  smartlink_init();
  pctel_init();
  intel_softmodem_init();
  motorola_sm56_init();
}
