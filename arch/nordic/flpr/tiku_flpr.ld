/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr.ld - FLPR (VPR RISC-V) linker script for every nRF54L part.
 *
 * Links the coprocessor image at the base of its 16 KB SRAM carve.  The M33
 * loader copies the whole binary there, so VMA == LMA and .data has no load
 * copy.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The top 1 KB of the carve is the shared IPC page (tiku_flpr_ipc.h) and the
 * stack grows down from just below it.  The geometry must match
 * TIKU_FLPR_RAM_* in tiku_flpr_ipc.h and the SRAM LENGTH in the app-core
 * scripts (nrf54l15.ld, nrf54lm20a.ld).
 */

MEMORY
{
    /* 16 KB carve minus the 1 KB shared page at the top. */
    FLPR_RAM (rwx) : ORIGIN = 0x2003C000, LENGTH = 0x3C00
}

ENTRY(_start)

SECTIONS
{
    .text :
    {
        KEEP(*(.init))              /* _start must sit at the carve base   */
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
    } > FLPR_RAM

    .data : ALIGN(4)
    {
        __global_pointer$ = . + 0x800;   /* gp-relative addressing window  */
        *(.sdata*)
        *(.data*)
        . = ALIGN(4);
    } > FLPR_RAM

    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(.sbss*)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > FLPR_RAM

    /* Stack: everything left in the carve, top-anchored just under the
     * shared page, which lies outside this region's LENGTH. */
    __stack_top = ORIGIN(FLPR_RAM) + LENGTH(FLPR_RAM);

    ASSERT(__bss_end <= __stack_top - 0x400,
           "FLPR image + 1 KB stack floor overflows the carve")
}
