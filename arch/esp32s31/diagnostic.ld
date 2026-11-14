/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * diagnostic.ld - S31 diagnostic layout in 64 KiB of low HP SRAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
ENTRY(tiku_s31_diagnostic_entry)
MEMORY { SRAM (rwx) : ORIGIN = 0x2f000000, LENGTH = 64K }
SECTIONS
{
    .text : ALIGN(64) {
        KEEP(*(.text.entry))
        *(.text .text.*)
        *(.rodata .rodata.* .srodata .srodata.*)
    } > SRAM
    .data : ALIGN(4) {
        *(.data .data.* .sdata .sdata.*)
        __global_pointer$ = . + 0x800;
    } > SRAM
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > SRAM
    __stack = ORIGIN(SRAM) + LENGTH(SRAM);
    __tier_sram_start = ALIGN(__bss_end, 64);
    __tier_sram_end = __stack - 4096;
    ASSERT(__bss_end <= __stack - 4096, "S31 diagnostic overlaps stack")
    .forbidden (NOLOAD) : {
        *(.persistent .persistent.* .retained .retained.*)
    } > SRAM
    ASSERT(SIZEOF(.forbidden) == 0, "S31 diagnostic must be storage-free")
    /DISCARD/ : { *(.comment .note .note.* .eh_frame .eh_frame.*) }
}
