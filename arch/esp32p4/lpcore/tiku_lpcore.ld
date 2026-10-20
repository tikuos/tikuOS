/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lpcore.ld - P4 LP core image, linked where it runs in LP SRAM.
 *
 * The HP loader copies the flat binary to 0x50108000, so VMA == LMA and .data
 * has no load copy.  The bounds repeat tiku_lpcore_ipc.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

ENTRY(_start)

MEMORY {
    LPRAM (rwx) : ORIGIN = 0x50108000, LENGTH = 0x6000
}

SECTIONS {
    .vectors 0x50108000 : {
        KEEP(*(.vectors))
    } > LPRAM

    .text 0x50108080 : {
        KEEP(*(.init))
        *(.text .text.*)
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
    } > LPRAM

    .data : ALIGN(4) {
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        *(.data .data.*)
        . = ALIGN(4);
    } > LPRAM

    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > LPRAM

    __stack_top = 0x5010e800;

    ASSERT(SIZEOF(.vectors) == 0x80, "LP vector table must be 32 four-byte slots")
    ASSERT(__bss_end <= 0x5010e000, "LP image overruns its stack")
    /DISCARD/ : { *(.comment .note .note.*) }
}
