/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp32c5_diagnostic.ld - RAM-loader diagnostic within the first 64 KiB.
 * The top 4 KiB holds its stack. HP SRAM above this window, including
 * ROM workspace, and LP SRAM are untouched.
 * SPDX-License-Identifier: Apache-2.0
 */
ENTRY(tiku_esp32c5_reset_handler)

MEMORY
{
    SRAM (rwx) : ORIGIN = 0x40800000, LENGTH = 64K
}

/* ESP-IDF 4d59230: components/esp_rom/esp32c5/ld/esp32c5.rom.ld. */
INCLUDE arch/esp32c5/devices/esp32c5_rom.ld

SECTIONS
{
    .text : ALIGN(4) {
        KEEP(*(.text.entry))
        *(.text .text.*)
        . = ALIGN(4);
    } > SRAM
    .rodata : ALIGN(4) {
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
    } > SRAM
    .data : ALIGN(4) {
        __data_start = .;
        *(.data .data.*)
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM
    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SRAM
    .uninit (NOLOAD) : ALIGN(32) {
        __uninit_start = .;
        KEEP(*(.persistent.layout))
        *(.persistent .persistent.*)
        . = ALIGN(4);
        __uninit_end = .;
    } > SRAM
    .retained (NOLOAD) : ALIGN(4) {
        *(.retained .retained.*)
        __retained_end = .;
    } > SRAM
    __stack = ORIGIN(SRAM) + LENGTH(SRAM);
    __stack_bottom = __stack - 4K;
    ASSERT(__retained_end <= __stack_bottom,
           "ESP32-C5 diagnostic overlaps its stack")
    ASSERT(__uninit_end - __uninit_start <= 16368,
           "ESP32-C5 durable data exceeds mirror payload")
    /DISCARD/ : { *(.comment .note .note.*) }
}
