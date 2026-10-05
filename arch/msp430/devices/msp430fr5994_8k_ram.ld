/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * msp430fr5994_8k_ram.ld - FR5994 8 KB SRAM override.
 *
 * Merges the stock script's three SRAM regions (RAM, LEARAM, LEASTACK) into
 * one 8 KB RAM region, since TikuOS does not use the LEA peripheral, holds
 * back the top of HIFRAM and fixes the high-bss clear size.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The stock msp430-elf-gcc script for the MSP430FR5994 splits the 8 KB of
 * SRAM into three regions, the last two for the LEA (Low-Energy
 * Accelerator) data and command stack:
 *
 *   RAM      : 0x1C00 length 0x1000  (4 KB general-purpose)
 *   LEARAM   : 0x2C00 length 0x0EC8  (~3.7 KB LEA data)
 *   LEASTACK : 0x3AC8 length 0x0138  (312 B LEA cmd stack)
 *
 * This file makes RAM all 8 KB and gives LEARAM and LEASTACK length 0.  The
 * stock `.leaRAM : {} > LEARAM` directive still links: an empty section in a
 * zero-length region produces no output.  The stock script places the stack
 * at ORIGIN(RAM) + LENGTH(RAM), so the initial SP is 0x3C00.
 *
 * A -T script given to msp430-elf-gcc 9.3 replaces the spec-default
 * msp430fr5994.ld, so this file INCLUDEs the stock script and then
 * redeclares MEMORY regions; ld keeps the last declaration of each region
 * name, and the regions not named here (FRAM, INFOMEM, vectors) stay as the
 * toolchain ships them.  The Makefile passes
 * -Tarch/msp430/devices/msp430fr5994_8k_ram.ld when MCU=msp430fr5994 and
 * LEA_ENABLE=0, and refuses LEA_ENABLE=1: a LEA layout needs its own script
 * that keeps the HIFRAM reservations below.
 */

INCLUDE msp430fr5994.ld

MEMORY {
  RAM      (rw) : ORIGIN = 0x1C00, LENGTH = 0x2000  /* 8 KB merged */
  LEARAM        : ORIGIN = 0x3C00, LENGTH = 0
  LEASTACK      : ORIGIN = 0x3C00, LENGTH = 0
  /* HIFRAM ends at 0x40FFF here; the stock script maps 0x10000..0x43FF7.
   * The top 12 KB is out of the linker's reach:
   *   0x41000..0x43000  pinned NVM backend (TIKU_NVMFS_MSP430_BASE in
   *                     tiku_nvm_region_msp430.c), separate from the
   *                     static MSP430 /data array
   *   0x43000..0x43FF0  loadable-module slot (TIKU_MODULE_CARVE_ADDR/SIZE
   *                     in tiku_basic_module.h), short of the odd CPU47
   *                     region end
   * Those two definitions name the same addresses. */
  HIFRAM  (rxw) : ORIGIN = 0x00010000, LENGTH = 0x00031000
}

/*
 * The crt zero-fills .upper.bss by clearing __high_bsssize bytes from
 * __high_bssstart, so the size must run from that marker to the end of the
 * section's content:
 *
 *   - SIZEOF(.upper.bss), the stock value, also counts the ALIGN(2) pad
 *     emitted below the marker when the section starts odd, and the clear
 *     then zeroes the first byte of the .upper.text function placed after
 *     the section.
 *   - __high_bssend - __high_bssstart misses the orphan .upper.bss input
 *     that the linker appends after the __high_bssend marker when
 *     .upper.bss is large (such as the BASIC arena).  That tail holds
 *     tiku_process_list_head, the event queue and the tick counters, and
 *     left uncleared it stalls the scheduler and the Timer A0 tick.
 *
 * ADDR(.upper.bss) + SIZEOF(.upper.bss) - __high_bssstart excludes the pad
 * and includes every byte placed in the section.  The last assignment to a
 * symbol wins, so this one, after the INCLUDE, replaces the stock value.
 */
__high_bsssize = ADDR(.upper.bss) + SIZEOF(.upper.bss) - __high_bssstart;

/*
 * __hifram_end marks the end of everything linked into HIFRAM, for the
 * `free` shell command; the stock script has end markers for .upper.bss
 * (__high_bssend) but none for .upper.text or .upper.rodata.
 *
 * SECTIONS blocks merge in order of appearance, so this NOLOAD section,
 * after the INCLUDE, lands after .upper.rodata, .upper.bss and .upper.text.
 * It takes no bytes; it records the location counter.
 *
 * ld resolves INSERT only against the spec-default script, not against
 * sections pulled in through INCLUDE: an insert after .upper.text fails here
 * with ".upper.text not found for insert".
 */
SECTIONS {
  .upper_end_marker (NOLOAD) :
  {
    . = ALIGN(2);
    __hifram_end = .;
  } > HIFRAM
}
