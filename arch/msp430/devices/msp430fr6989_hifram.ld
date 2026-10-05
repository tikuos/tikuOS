/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * msp430fr6989_hifram.ld - Fixed persistent data and HIFRAM reservations.
 *
 * Pins the .persistent origin in lower FRAM, holds back the top of HIFRAM
 * for the NVM backend and the module slot, and fixes the high-bss clear size.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 0x4C6C is the .persistent origin of the minimal large-model image, so a
 * board running that image keeps its persistent data across a reflash.  Data
 * written by any other image is reusable only after an object-by-object ELF
 * comparison: an equal origin does not mean an equal persistent layout. */
PROVIDE(__tiku_fr6989_persistent_base = 0x4c6c);

MEMORY {
  FRAM (rx) : ORIGIN = 0x4400, LENGTH = 0xbb80
}

/* This section takes the .persistent inputs before the stock script packs
 * the .either sections.  Lower FRAM below the fixed origin is left
 * unallocated.  An override of __tiku_fr6989_persistent_base must be the
 * .persistent origin of the image whose data is kept, not any free address. */
SECTIONS {
  .persistent __tiku_fr6989_persistent_base :
  {
    . = ALIGN(2);
    __persistent_start = .;
    *(.persistent)
    . = ALIGN(2);
    __persistent_end = .;
  } > FRAM
}
INSERT BEFORE .lower.rodata;

INCLUDE msp430fr6989.ld

ASSERT((__tiku_fr6989_persistent_base & 1) == 0,
       "FR6989 persistent origin must be word aligned")
ASSERT(__tiku_fr6989_persistent_base >= ORIGIN(FRAM) &&
       __persistent_end <= ORIGIN(FRAM) + LENGTH(FRAM),
       "FR6989 persistent span must fit lower FRAM")

/* HIFRAM ends at 0x20FFF here.  0x21000..0x23000 is the pinned NVM backend
 * (TIKU_NVMFS_MSP430_BASE), separate from the static /data array, and
 * 0x23000..0x23ff0 the module slot (TIKU_MODULE_CARVE_ADDR), below the CPU47
 * boundary.  Neither reservation is available to linked sections. */
MEMORY {
  HIFRAM (rxw) : ORIGIN = 0x00010000, LENGTH = 0x00011000
}

/* The startup clear runs from __high_bssstart to the end of .upper.bss: the
 * leading alignment pad stays out, and orphan .upper.bss inputs placed after
 * __high_bssend stay in.  The code that follows the section is not cleared. */
__high_bsssize = ADDR(.upper.bss) + SIZEOF(.upper.bss) - __high_bssstart;

/* __hifram_end follows every HIFRAM section of the included script and marks
 * the end of HIFRAM use for the `free` shell command; it takes no bytes. */
SECTIONS {
  .upper_end_marker (NOLOAD) :
  {
    . = ALIGN(2);
    __hifram_end = .;
  } > HIFRAM
}
