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
 * This file therefore only adds the HIFRAM marker; it doesn't touch
 * the MEMORY block.)
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

SECTIONS {
  .upper_end_marker (NOLOAD) :
  {
    . = ALIGN(2);
    __hifram_end = .;
  } > HIFRAM
}
