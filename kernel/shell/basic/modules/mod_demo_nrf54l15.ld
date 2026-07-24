/*
 * Tiku Operating System v0.06 -- loadable native module linker script.
 *
 * The module is position-FIXED at the carve VMA (TIKU_MODULE_CARVE_ADDR): the
 * loader copies the flat image there and jumps in, so VMA == LMA == the carve
 * base and every internal reference resolves at run address.  Layout:
 *   +0    .modhdr   (tiku_module_header_t, exactly 16 bytes)
 *   +16   .modinit  (module_init entry -- header's init_off points here)
 *   ...   .text / .rodata
 * No .data/.bss: the MVP module holds no writable state (pure handlers), so a
 * missing RAM/copy step cannot bite.  Keep in sync with the header + device .ld.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

MEMORY
{
    /* Executable RRAM slot (nordic W^X forbids SRAM execution), L15 geometry.
     * Kept in sync with __tiku_module_slot / TIKU_MODULE_CARVE_ADDR. */
    CARVE (rx) : ORIGIN = 0x0B8000, LENGTH = 0x8000   /* 32 KB */
}

ENTRY(module_init)

SECTIONS
{
    .mod ORIGIN(CARVE) :
    {
        KEEP(*(.modhdr))
        . = ORIGIN(CARVE) + 16;      /* module_init lands exactly at +16 */
        KEEP(*(.modinit))
        *(.text*)
        *(.rodata*)
        *(.rodata.*)
        . = ALIGN(4);
    } > CARVE

    ASSERT(SIZEOF(.mod) <= LENGTH(CARVE), "module image overflows the carve")

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note*)
        *(.debug*)
        *(.bss*)
        *(.data*)
    }
}
