/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * flash.ld - S31 SRAM runtime with two bootloader-mapped flash regions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
ENTRY(tiku_s31_diagnostic_entry)
MEMORY {
    SRAM (rwx) : ORIGIN = 0x2f000000, LENGTH = 64K
    DROM (r) : ORIGIN = 0x40000020, LENGTH = 64K - 32
    IROM (rx) : ORIGIN = 0x40010020, LENGTH = 64K - 32
}
SECTIONS
{
    .flash.rodata : {
        KEEP(*(.app_desc))
        *(.rodata .rodata.* .srodata .srodata.*)
    } > DROM
    .flash.text : {
        *(.flash.text)
    } > IROM
    .text : ALIGN(64) {
        KEEP(*(.text.entry))
        *(.text .text.*)
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
