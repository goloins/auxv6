# HPET + ACPI bring-up plan for auxv6

## Overview

This note captures what auxv6 would need in order to gain a usable HPET timer driver and the ACPI plumbing required to discover and configure it on real hardware.

The current kernel timer path is centered on the local APIC timer, with the periodic interrupt feeding:

- global tick advancement
- scheduler preemption
- per-process CPU accounting
- alarm delivery
- load-average maintenance
- periodic USB and network service work

Because of that, HPET support should be added in stages. The safest path is to introduce HPET first as a discovered hardware clock source and only later consider whether it should replace or supplement the LAPIC timer.

---

## Current kernel assumptions

Today the kernel assumes:

1. SMP is discovered from Intel MP tables.
2. The LAPIC exists and is initialized early during boot.
3. The timer interrupt arrives on the legacy timer IRQ vector path.
4. Much of the system expects a regular 100 Hz heartbeat.

That means a pure “swap LAPIC out for HPET” change would be risky. HPET can improve timekeeping and periodic interrupt generation, but the kernel still needs a coherent story for per-CPU scheduling and interrupt delivery.

---

## Main gaps to close

### 1. ACPI discovery layer

There is currently no ACPI subsystem for table discovery. To bring up HPET cleanly on modern systems, the kernel needs at minimum:

- RSDP scan in BIOS memory / boot handoff memory
- RSDT or XSDT parsing
- checksum verification for each system description table
- MADT parsing for APIC topology and interrupt overrides
- HPET table parsing to discover the HPET base address and capabilities
- FADT parsing only if later power-management or legacy replacement behavior is needed

Without ACPI, HPET can only be hardcoded to a guessed address, which is fragile and not appropriate for real bring-up.

### 2. HPET register definitions and MMIO driver

A new driver should define:

- general capabilities register
- general configuration register
- general interrupt status register
- main counter value register
- timer comparator registers
- per-timer configuration bits

The driver needs helpers to:

- map and validate the MMIO base
- determine counter period from femtosecond units
- start and stop the main counter
- program a periodic or one-shot comparator
- acknowledge timer interrupts correctly

### 3. Interrupt routing model

HPET interrupts do not help unless the comparator output reaches the CPU through a working route.

The kernel will need to support one of these:

- HPET legacy replacement mode, if available and desired
- routing the HPET comparator through the IOAPIC to an existing vector
- direct FSB interrupt delivery if later supported

For initial bring-up, the simplest path is usually one comparator routed through the IOAPIC to the existing timer vector.

### 4. Timer abstraction in the kernel core

The trap path and timekeeping code currently assume one hardware source. To make HPET integration clean, introduce a small clock-event abstraction so the rest of the kernel only sees:

- timer source initialization
- periodic tick start
- optional one-shot next-event programming
- monotonic clock reads

This avoids burying HPET-specific logic directly into generic trap or scheduler code.

---

## Recommended staged implementation

## Phase 0: Preparation and refactor

Before touching HPET, separate policy from hardware details.

### Goals

- keep the current kernel booting exactly as it does now
- isolate the common tick-handling path from LAPIC-specific programming
- make it possible to switch timer backends by configuration or probe result

### Concrete changes

1. Create a generic timer core, such as:
   - kernel/core/timer.c
   - include timer declarations in the shared kernel defs

2. Move the non-hardware tick work into one function, for example:
   - increment ticks
   - wake sleepers
   - run alarm checks
   - update load averages
   - service periodic background work

3. Keep LAPIC as the initial implementation of this interface.

### Deliverable

The system should still boot and behave identically, but timer hardware setup and timer tick handling are no longer tightly coupled.

---

## Phase 1: Minimal ACPI support

This phase is about finding the hardware safely.

### Files likely to add

- kernel/driver/acpi.c
- include/acpi.h

### Structures to support first

- RSDP
- RSDT and XSDT
- generic SDT header
- MADT header and relevant APIC subtables
- HPET table

### Bring-up steps

1. Scan for the RSDP in the conventional BIOS search areas.
2. Verify the RSDP checksum.
3. Load the RSDT or XSDT.
4. Enumerate child tables by signature.
5. Locate MADT and HPET.
6. Export parsed results through a small kernel API.

### Suggested API shape

Examples of useful interfaces:

- acpi_init()
- acpi_find_table(signature)
- acpi_get_hpet_info(...)
- acpi_get_interrupt_override(...)

### Validation

At this stage, printing verified boot diagnostics is enough:

- RSDP found
- XSDT or RSDT loaded
- MADT found
- HPET found at a specific base address
- number of HPET timers reported
- counter period reported

---

## Phase 2: HPET MMIO driver

This phase turns discovery into a usable hardware block.

### Files likely to add

- kernel/driver/hpet.c
- include/hpet.h

### Core functionality

Implement:

- hpet_init()
- hpet_available()
- hpet_read_counter()
- hpet_start_periodic_tick(freq_hz)
- hpet_stop()

### Driver bring-up sequence

1. Read HPET capabilities.
2. Confirm the main counter is 64-bit if available, otherwise note 32-bit limitations.
3. Disable the HPET while programming it.
4. Zero or sample the main counter.
5. Pick a comparator that supports periodic mode.
6. Program comparator interval for 100 Hz.
7. Enable interrupt generation.
8. Start the main counter.

### Things to verify carefully

- the femtoseconds-per-tick value converts correctly to Hertz
- the comparator chosen actually supports periodic mode
- writes to the comparator and configuration registers stick
- the interrupt enable bit is set in the intended timer block

### Early debug output worth adding

- HPET vendor and capability bits
- number of timers
- counter period in femtoseconds
- selected comparator index
- chosen interrupt route

---

## Phase 3: Interrupt delivery and routing

HPET configuration alone is not enough; the interrupt must land on a usable vector.

### Initial target

Use the existing timer vector so the rest of the kernel remains unchanged.

### Recommended approach

1. Choose a free HPET comparator.
2. Program it to emit periodic interrupts.
3. Route that output through the IOAPIC.
4. Deliver it to the current timer interrupt vector.

### Possible complications

- interrupt source overrides discovered from MADT
- edge vs level trigger requirements
- active-low polarity on some routes
- differences between emulators and real chipsets

### Practical advice

For first bring-up, keep the route simple and avoid trying to support every routing mode immediately.

---

## Phase 4: Integrate with the kernel tick path

Once HPET interrupts are firing, wire them into the same tick handler used today.

### Immediate goal

Preserve current behavior exactly:

- one global tick increments at 100 Hz
- user-visible uptime behavior remains stable
- scheduler quanta still behave the same
- sleep and alarm logic still works

### Important note for SMP

The current kernel uses timer-driven work on all CPUs, especially for CPU accounting and preemption. A single global HPET interrupt cannot automatically replace the role of per-CPU LAPIC timers.

Because of that, the preferred initial integration is:

- keep LAPIC timers for per-CPU scheduling if needed
- use HPET as a better clock source and optionally as the BSP periodic tick
- only consider replacing LAPIC scheduling ticks after the kernel has explicit broadcast or per-CPU timer logic

This is the most important architectural caution in the whole effort.

---

## Phase 5: Optional improvements after initial success

After basic HPET support works, these become realistic follow-on tasks:

### A. Better timekeeping

Use HPET for monotonic reads instead of relying primarily on TSC interpolation.

Benefits:

- more stable early bring-up on systems with poor TSC behavior
- easier calibration of other clocks
- clearer wall-clock and monotonic implementations

### B. One-shot timers

Move from a fixed 100 Hz periodic tick toward one-shot next-event scheduling.

Benefits:

- lower idle interrupt overhead
- more precise sleeps and timeouts
- a cleaner path toward high-resolution timers

### C. LAPIC calibration

Use HPET as a reference source to calibrate the LAPIC timer accurately rather than using a fixed initial count guess.

This is probably the best medium-term improvement if you want to keep the current SMP scheduling model.

---

## Suggested development order

A practical order of work for this repository would be:

1. Refactor the tick-handling path into a generic timer core.
2. Add minimal ACPI table discovery with checksums.
3. Parse MADT and HPET tables.
4. Add the HPET MMIO driver with counter readout.
5. Print HPET diagnostics during boot.
6. Program one comparator for a test interrupt.
7. Route that interrupt into the existing timer vector.
8. Verify that uptime, sleep, and scheduler behavior still work.
9. Only then decide whether HPET supplements or replaces part of the LAPIC timer usage.

---

## Risks and pitfalls

### 1. Assuming HPET replaces LAPIC everywhere

On SMP, that can break fairness, preemption, and CPU-local accounting.

### 2. Skipping ACPI interrupt overrides

Modern systems may not route interrupts the way old MP-table logic expects.

### 3. Hardcoding the common HPET address

Many systems use the standard address, but discovery must still be table-driven.

### 4. Mixing timekeeping and event delivery logic

Keep “what time is it?” separate from “when should the next interrupt fire?”

### 5. Trying for high-resolution timers too early

Bring up a stable 100 Hz periodic tick first. Precision work can come later.

---

## Proposed code organization

A clean structure could look like this:

- kernel/driver/acpi.c
- kernel/driver/hpet.c
- kernel/core/timer.c
- include/acpi.h
- include/hpet.h

And the boot flow would become conceptually:

1. mpinit()
2. acpi_init()
3. lapicinit()
4. ioapicinit()
5. timer_backend_init()
6. enable periodic system tick

---

## Suggested acceptance criteria

The initial HPET bring-up should count as successful when all of the following are true:

- the kernel finds and validates ACPI tables on boot
- the HPET table is discovered and decoded
- the HPET main counter can be read reliably
- one comparator can generate interrupts at the desired interval
- the system tick continues at 100 Hz
- sleep, uptime, alarms, and scheduler behavior still work normally
- SMP boot still succeeds

---

## Recommended first milestone

The best first milestone is not “replace the LAPIC timer.”

It is:

> Discover HPET via ACPI, expose a readable monotonic counter, and prove the kernel can receive a periodic HPET interrupt without regressing current timer behavior.

That milestone reduces risk while creating the infrastructure needed for future improvements.

---

## Summary

To add an HPET timer driver to auxv6, the kernel needs:

- a minimal ACPI subsystem for table discovery
- a real HPET MMIO driver
- interrupt routing support through the existing APIC infrastructure
- a small timer abstraction layer in the core kernel
- careful SMP-aware integration so current scheduling assumptions are preserved

The safest and most effective path is to make HPET an additional timing facility first, then decide later whether deeper replacement of LAPIC-driven timing is worthwhile.
