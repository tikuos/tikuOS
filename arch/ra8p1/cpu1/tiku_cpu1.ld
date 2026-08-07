/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1.ld - layout of the RA8P1 Cortex-M33 payload.
 *
 * Links at 0 and stays position-independent: the loader copies the image to
 * a 128-byte-aligned buffer and patches only the first two vector words.
 * The offsets come from tiku_cpu1_ipc.h, which both toolchains compile.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

ENTRY(cpu1_reset)

SECTIONS
{
    /* Sixteen words.  The loader patches [0] = SP and [1] = entry | 1. */
    .vectors 0x00 : { KEEP(*(.cpu1_vectors)) }

    /* Pinned at TIKU_CPU1_RESET_OFF, which the loader writes into vector 1.
     * The entry must come first: the payload derives its base by masking the
     * PC, which is exact only inside the first 128 bytes of the image. */
    .text 0x40 : { KEEP(*(.cpu1_reset)) *(.text*) *(.rodata*) }

    __text_end = ADDR(.text) + SIZEOF(.text);

    /* TIKU_CPU1_SHARED_OFF.  Zero-filled here and by the loader; the two
     * halves of the struct sit on separate cache lines, which the header
     * asserts rather than this script. */
    .shared 0x200 (NOLOAD) : { . = . + 0x80; }

    /DISCARD/ : { *(.ARM.*) *(.comment) *(.debug*) *(.note*) }
}

ASSERT(SIZEOF(.vectors) == 0x40,
       "cpu1: the vector table must be exactly 16 words")

/* The payload must not grow into the page it talks through. */
ASSERT(__text_end <= 0x200,
       "cpu1: code overruns the shared page")

/* The PC-derived base is exact only while the entry sits inside the first
 * 128 bytes; past that it resolves one line low and the payload writes over
 * its own vector table. */
ASSERT(ADDR(.text) + 0x40 <= 0x80,
       "cpu1: reset code leaves the window its base derivation needs")
