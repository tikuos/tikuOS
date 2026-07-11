/*
 * Tiku Operating System v0.05 -- FLPR (VPR RISC-V) linker script, nRF54L15.
 *
 * The image is fully position-fixed in the SRAM carve (VMA == LMA == the
 * carve base): the Cortex-M33 loader memcpys the whole binary there, so
 * there is no separate load region and no .data LMA.  The top 1 KB of the
 * carve is the shared IPC page (tiku_flpr_ipc.h) and the stack grows down
 * from just below it.  Keep the geometry in sync with TIKU_FLPR_RAM_* in
 * tiku_flpr_ipc.h and the reserve in the app-core nrf54l15.ld.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0
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
     * shared page (which lies OUTSIDE this region's LENGTH). */
    __stack_top = ORIGIN(FLPR_RAM) + LENGTH(FLPR_RAM);

    ASSERT(__bss_end <= __stack_top - 0x400,
           "FLPR image + 1 KB stack floor overflows the carve")
}
