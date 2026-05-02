/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * msp430fr5994_8k_ram.ld - FR5994 8 KB SRAM override
 *
 * Why this exists
 * ===============
 * The stock msp430-elf-gcc linker script for MSP430FR5994 carves the
 * 8 KB on-chip SRAM into three regions to keep the LEA (Low-Energy
 * Accelerator) peripheral's data and command-stack buffers from
 * colliding with general .bss/stack:
 *
 *   RAM      : 0x1C00 length 0x1000  (4 KB general-purpose)
 *   LEARAM   : 0x2C00 length 0x0EC8  (~3.7 KB LEA data)
 *   LEASTACK : 0x3AC8 length 0x0138  (312 B LEA cmd stack)
 *
 * TikuOS does not use the LEA peripheral by default, so reserving
 * those last 4 KB of SRAM is just lost address space. This fragment
 * collapses the three regions into one 8 KB RAM region, giving the
 * kernel + processes the full physical SRAM the chip has to offer.
 *
 * The stock script's `.leaRAM : {} > LEARAM` section directive still
 * resolves cleanly because LEARAM is redeclared with LENGTH=0
 * (an empty section landing in a zero-length region produces no
 * output, which is exactly the behaviour we want).
 *
 * The stack origin auto-follows: stock script uses
 *   .stack (ORIGIN(RAM) + LENGTH(RAM)) : ...
 * so growing RAM moves the initial SP from 0x2C00 to 0x3C00 with no
 * other change.
 *
 * How it's wired in
 * =================
 * Passing -T <file> on msp430-elf-gcc 9.3 *replaces* the spec-default
 * linker script — the spec-driven `-T msp430fr5994.ld` is suppressed
 * the moment a user-supplied -T appears. So we INCLUDE the stock
 * script first to pick up all the SECTIONS / vector tables / FRAM
 * layout, then redeclare just the three SRAM-side MEMORY regions.
 * ld(1)'s "last MEMORY declaration of a given name wins" rule applies
 * region-by-region, leaving everything else (FRAM, HIFRAM, INFOMEM,
 * vectors, etc.) exactly as the toolchain shipped it.
 *
 * The Makefile prepends this fragment via:
 *   ifeq ($(MCU),msp430fr5994)
 *   ifeq ($(LEA_ENABLE),0)
 *   LDFLAGS += -Tarch/msp430/devices/msp430fr5994_8k_ram.ld
 *
 * To get the stock 4 KB + LEA layout back:
 *   make MCU=msp430fr5994 LEA_ENABLE=1
 *
 * SPDX-License-Identifier: Apache-2.0
 */

INCLUDE msp430fr5994.ld

MEMORY {
  RAM      (rw) : ORIGIN = 0x1C00, LENGTH = 0x2000  /* 8 KB merged */
  LEARAM        : ORIGIN = 0x3C00, LENGTH = 0
  LEASTACK      : ORIGIN = 0x3C00, LENGTH = 0
}

/*
 * Expose the end-of-HIFRAM address as a linker symbol so the `free`
 * shell command can report HIFRAM usage. The stock LD provides
 * __high_bssstart / __high_bssend for .upper.bss but no end markers
 * for .upper.text or .upper.rodata, so without this the kernel can
 * only report total HIFRAM reachability (208 KB) and not what's
 * actually consumed by code/rodata/bss.
 *
 * Multiple SECTIONS blocks in a single linker script are merged
 * into one effective SECTIONS command, with sections placed in the
 * order they appear across all blocks. Putting this NOLOAD marker
 * after the INCLUDE'd standard script means it lands after every
 * standard HIFRAM-targeted section (.upper.rodata at :247,
 * .upper.bss at :337, .upper.text at :419), so __hifram_end
 * captures the end of *all* HIFRAM-resident sections. NOLOAD means
 * the marker contributes no bytes — we're just snapshotting the
 * location counter.
 *
 * (We previously tried INSERT AFTER .upper.text but ld errors out
 * with ".upper.text not found for insert" — INSERT only resolves
 * against sections in the spec-default linker script, not against
 * sections pulled in via INCLUDE.)
 */
SECTIONS {
  .upper_end_marker (NOLOAD) :
  {
    . = ALIGN(2);
    __hifram_end = .;
  } > HIFRAM
}
