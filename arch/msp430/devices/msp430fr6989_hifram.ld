/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * msp430fr6989_hifram.ld - FR6989 HIFRAM end-marker override
 *
 * Why this exists
 * ===============
 * FR6989 has 80 KB of HIFRAM at 0x10000+ but the toolchain's stock
 * msp430fr6989.ld provides no end-of-HIFRAM symbol — only
 * __high_bssstart / __high_bssend for .upper.bss are exposed, which
 * doesn't help when .upper.text or .upper.rodata extend past
 * .upper.bss in HIFRAM.
 *
 * Without an end marker the kernel's `free` shell command can only
 * report total HIFRAM reachability and not actual usage. Same gap
 * the FR5994 override fixes, same fix here. (FR6989 doesn't need
 * the SRAM merging that FR5994 does — FR6989 only has 2 KB of SRAM
 * and no LEA peripheral, so the stock RAM region is already optimal.
 * Besides the marker, this file also re-declares HIFRAM to hold back
 * the top 4 KB as the Tier-3 module slot.)
 *
 * Implementation notes
 * ====================
 * msp430-elf-gcc 9.3 *replaces* the spec-default linker script when
 * `-T` is passed, rather than augmenting it. The fragment below
 * INCLUDEs the stock script verbatim, then adds a NOLOAD marker
 * section that targets HIFRAM. Multiple SECTIONS commands in a
 * single linker script are merged in declaration order, so the
 * marker lands after every standard HIFRAM-targeted section
 * (.upper.rodata, .upper.bss, .upper.text). __hifram_end captures
 * the location counter at marker entry — the end of all
 * HIFRAM-resident sections.
 *
 * Wired in from the Makefile when MCU=msp430fr6989. No flag is
 * needed to disable it; the override is always-on for FR6989.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

INCLUDE msp430fr6989.ld

/* Held-back HIFRAM top ("last MEMORY declaration of a given name wins",
 * region-by-region): the stock script maps HIFRAM 0x10000..0x23FF7.
 * The top 12 KB is reserved, out of the linker's reach:
 *   0x21000..0x23000  pinned /data region (tiku_nvm_region_msp430.c) --
 *                     a FIXED address, so /data survives reflashes
 *   0x23000..0x23FF0  Tier-3 loadable-module slot (tiku_basic_module.h;
 *                     stops shy of the CPU47 region end)
 * Keep in sync with TIKU_NVMFS_MSP430_BASE. */
MEMORY {
  HIFRAM (rxw) : ORIGIN = 0x00010000, LENGTH = 0x00011000
}

/*
 * Corrected high-bss clear size -- see the detailed note in
 * msp430fr5994_8k_ram.ld.  The crt clears __high_bsssize bytes from
 * __high_bssstart, which must be the exact byte count to the end of
 * .upper.bss content: too large stomps the first opcode of the
 * following .upper.text function (the odd-start ALIGN-pad overshoot);
 * too small leaves live kernel .bss (process list, event queue, tick
 * counters) as stale FRAM garbage and wedges boot (what
 * __high_bssend - __high_bssstart does once .upper.bss is large --
 * the linker appends orphan .upper.bss input past the __high_bssend
 * marker, so end-start covers only part of the section; confirmed on
 * fr5994 with BASIC's ~64 KB arena).  (__high_bsssize) below is
 * robust against both.  This is DISTINCT from the historical fr6989
 * large-bss boot-loop -- a WDT timeout during the clear, fixed in
 * tiku_crt_early.c.
 */
__high_bsssize = ADDR(.upper.bss) + SIZEOF(.upper.bss) - __high_bssstart;

SECTIONS {
  .upper_end_marker (NOLOAD) :
  {
    . = ALIGN(2);
    __hifram_end = .;
  } > HIFRAM
}
