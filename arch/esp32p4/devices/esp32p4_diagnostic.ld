/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * esp32p4_diagnostic.ld - Storage-free native diagnostic in low HP SRAM.
 *
 * The whole image links into 64 KiB of HP SRAM at 0x4ff00000 with a 4 KiB
 * stack at its top; the 64-byte fault record sits below the LP ROM data at
 * 0x5010fa80.  Any other .persistent or .retained section fails the link.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
ENTRY(tiku_esp32p4_reset_handler)
MEMORY {
    SRAM (rwx) : ORIGIN = 0x4ff00000, LENGTH = 64K
    /* LP ROM stack/data starts at 0x5010fa80; the LP core stays in reset. */
    FAULT (rw) : ORIGIN = 0x5010fa40, LENGTH = 64
}
SECTIONS
{
    .text : ALIGN(64) {
        KEEP(*(.text.entry))
        *(.text .text.*)
        . = ALIGN(4);
    } > SRAM
    .rodata : ALIGN(4) {
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
    } > SRAM
    .data : ALIGN(4) {
        *(.data .data.*)
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > SRAM
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > SRAM
    .fault_record (NOLOAD) : {
        KEEP(*tiku_fault_arch.o(.retained))
    } > FAULT
    ASSERT(SIZEOF(.fault_record) == 64, "P4 fault record must be 64 bytes")
    .forbidden (NOLOAD) : {
        *(.persistent .persistent.* .retained .retained.*)
    } > SRAM
    __stack = ORIGIN(SRAM) + LENGTH(SRAM);
    __stack_bottom = __stack - 4K;
    ASSERT(__bss_end <= __stack_bottom, "P4 diagnostic overlaps stack")
    ASSERT(SIZEOF(.forbidden) == 0, "P4 diagnostic must be storage-free")
    /DISCARD/ : { *(.comment .note .note.*) }
}
