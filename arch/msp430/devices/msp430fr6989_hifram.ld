/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * msp430fr6989_hifram.ld - Fixed persistent data and HIFRAM reservations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* This origin preserves the minimal large-model firmware profile. Other
 * installed images require an object-by-object ELF comparison before reuse.
 * A matching origin alone does not guarantee a matching persistent format. */
PROVIDE(__tiku_fr6989_persistent_base = 0x4c6c);

MEMORY {
  FRAM (rx) : ORIGIN = 0x4400, LENGTH = 0xbb80
}

/* Consume persistent inputs before the stock script packs .either sections.
 * The lower-bank prefix before the fixed origin is not allocated. An address
 * override must come from the reference firmware, not available free space. */
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

/* 0x21000..0x23000 is the pinned backend, separate from the static /data
 * array. 0x23000..0x23ff0 is the module slot, below the CPU47 boundary.
 * Neither reservation is available to linked sections. */
MEMORY {
  HIFRAM (rxw) : ORIGIN = 0x00010000, LENGTH = 0x00011000
}

/* Exclude leading alignment and include orphan .upper.bss inputs in the
 * startup clear. The following code must not be cleared. */
__high_bsssize = ADDR(.upper.bss) + SIZEOF(.upper.bss) - __high_bssstart;

SECTIONS {
  .upper_end_marker (NOLOAD) :
  {
    . = ALIGN(2);
    __hifram_end = .;
  } > HIFRAM
}
