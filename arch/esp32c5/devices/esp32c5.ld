/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp32c5.ld - C5 SRAM image, durable working copy, retained state and allocator.
 * The application limit follows ESP-IDF 4d59230 ld.hp_mem_defs (ROM exclusion).
 * SPDX-License-Identifier: Apache-2.0
 */
ENTRY(tiku_esp32c5_reset_handler)
MEMORY {
    SRAM (rwx) : ORIGIN = 0x40800000, LENGTH = 0x4E5A0
    XIP (rx) : ORIGIN = 0x42100000, LENGTH = 0x100000
}
INCLUDE arch/esp32c5/devices/esp32c5_rom.ld
__sram_start = ORIGIN(SRAM);
__sram_end = ORIGIN(SRAM) + LENGTH(SRAM);
__tiku_layout_code_cap = 0x42200000;
__tiku_code_limit = __tiku_layout_code_cap;
__tiku_layout_persist_size = 0x8000;
__tiku_layout_scratch_size = 0x1000;
__tiku_c5_xip_bytes = 0;
SECTIONS
{
    INCLUDE tiku_c5_xip.ld
    .text : ALIGN(4) {
        KEEP(*(.text.entry))
        *(.text .text.* .iram1 .iram1.*)
        *(.high_perf_code_iram1 .high_perf_code_iram1.*)
        *(.coexiram .coexiram.*)
        . = ALIGN(4);
    } > SRAM
    .rodata : ALIGN(4) {
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
        __tiku_cells_start = .;
        KEEP(*(.tiku_cells))
        __tiku_cells_end = .;
        . = ALIGN(4);
    } > SRAM
    _etext = .;
    .c5.expected : ALIGN(4) { KEEP(*(.c5.expected)) } > SRAM
    .data : ALIGN(4) {
        __data_start = .;
        *(.data .data.* .dram1 .dram1.*)
        . = ALIGN(8);
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM
    __data_load = LOADADDR(.data);
    __datastart = ORIGIN(SRAM);
    __tiku_flash_payload_bytes = SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data)
                                + SIZEOF(.c5.expected) + __tiku_c5_xip_bytes;
    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SRAM
    .uninit (NOLOAD) : ALIGN(32) {
        __uninit_start = .;
        KEEP(*(.persistent.layout))
        *(.persistent .persistent.* .uninit .uninit.*)
        . = ALIGN(4);
        __uninit_end = .;
    } > SRAM
    .retained (NOLOAD) : ALIGN(4) {
        __retained_start = .;
        *(.retained .retained.*)
        . = ALIGN(4);
        __retained_end = .;
    } > SRAM
    _end = .;
    PROVIDE(end = _end);
    __stack = __sram_end;
    __stack_bottom = __stack - 16K;
    __tiku_stack_bottom = __stack_bottom;
    __tiku_stack_guard_start = __stack_bottom;
    __tiku_tier_span_start = ALIGN(__retained_end, 32);
    __tiku_tier_span_limit = __stack_bottom;
    ASSERT(!DEFINED(__tiku_c5_sdr) || (__tiku_tier_span_start <= 0x40820000 &&
           __tiku_tier_span_limit >= 0x40840000),
           "C5 SDR requires the full 128 KiB bank at 0x40820000")
    __tiku_tier_reserve_default = 0;
    __tiku_tier_floor_default = 32768;
    INCLUDE arch/common/tiku_sram_layout.ld
    ASSERT(__uninit_end - __uninit_start <= 16368,
           "ESP32-C5 durable data exceeds mirror payload")
    ASSERT((DEFINED(tiku_layout_pin) ? tiku_layout_pin : __uninit_start) == __uninit_start,
           "ESP32-C5 layout record must open the durable span")
    /DISCARD/ : { *(.comment .note .note.*) }
}
