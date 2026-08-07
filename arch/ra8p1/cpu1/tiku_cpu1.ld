/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1.ld - layout of the RA8P1 Cortex-M33 payload.
 *
 * Links at the fixed carve r7ka8p1kf.ld reserves; the loader copies the
 * image there and patches SP, reset and HardFault into its vector table.
 * The offsets come from tiku_cpu1_ipc.h, which both toolchains compile.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

ENTRY(cpu1_reset)

/* The fixed carve the device script reserves; absolute addresses the
 * compiler emits (a global table's literal, a function pointer) are correct
 * only because the image runs exactly here.  Keep in step with
 * TIKU_CPU1_AREA_ADDR in tiku_cpu1_ipc.h and the carve in r7ka8p1kf.ld. */

BASE = 0x22000000;

SECTIONS
{
    /* Sixteen words.  The loader patches [0] = SP and [1] = entry | 1. */
    .vectors BASE : { KEEP(*(.cpu1_vectors)) }

    /* Pinned at TIKU_CPU1_RESET_OFF, which the loader writes into vector 1.
     * The entry must come first: the payload derives its base by masking the
     * PC, which is exact only inside the first 128 bytes of the image. */
    .text BASE + 0x40 : { KEEP(*(.cpu1_reset)) *(.text*) *(.rodata*) }

    /* Zero-initialised data lives inside the carve, so the loader's
     * zero-fill IS the .bss clear -- there is no crt to run one. */
    .bss (NOLOAD) : ALIGN(8) {
        __bss_start = .;
        *(.bss*) *(COMMON)
        __bss_end = .;
    }
    __text_end = __bss_end;

    /* HardFault handler, pinned where the loader points vector 3. */
    .fault BASE + 0x1800 : { KEEP(*(.cpu1_fault)) }
    __fault_end = ADDR(.fault) + SIZEOF(.fault);

    /* TIKU_CPU1_SHARED_OFF.  Zero-filled here and by the loader; the two
     * halves of the struct sit on separate cache lines, which the header
     * asserts rather than this script. */
    .shared BASE + 0x1900 (NOLOAD) : { . = . + 0x1C0; }

    /DISCARD/ : { *(.ARM.*) *(.comment) *(.debug*) *(.note*) }
}

ASSERT(SIZEOF(.vectors) == 0x40,
       "cpu1: the vector table must be exactly 16 words")

/* The payload must not grow into the fault handler's slot, nor the fault
 * handler into the page both talk through. */
ASSERT(__text_end <= BASE + 0x1800,
       "cpu1: code and bss overrun the fault handler at 0x1800")
ASSERT(__fault_end <= BASE + 0x1900,
       "cpu1: fault handler overruns the shared page")

/* The PC-derived base is exact only while the entry sits inside the first
 * 128 bytes; past that it resolves one line low and the payload writes over
 * its own vector table. */
ASSERT(ADDR(.text) + 0x40 <= BASE + 0x80,
       "cpu1: reset code leaves the window its base derivation needs")
