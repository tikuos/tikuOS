/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * esp32c61.ld - ESP32-C61 linker script.
 *
 * The image lives in HP SRAM, where the ROM loads it and jumps to the entry.
 * Flash is storage, and a code window only for what a build places in XIP
 * below (the radio's libraries): `make` writes that part as xip.bin.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Memory map (HP SRAM, 320 KB at 0x40800000, one space for code and data):
 *   0x40800000  .text, .rodata, .data, .bss, .uninit, then the SRAM tier
 *   0x4084C000  __stack, growing down into a reserve the tier never takes
 *   0x4084C670  the ROM's stack and data, to the top -- left alone: its delay
 *               and flash routines keep their state there
 *
 * Symbols the kernel expects:
 *   __stack, _end, _etext, __data_load, __data_start, __data_end,
 *   __bss_start, __bss_end, __sram_start, __sram_end, __uninit_start/end
 */

ENTRY(tiku_esp32c61_reset_handler)

MEMORY
{
    SRAM (rwx) : ORIGIN = 0x40800000, LENGTH = 0x4C000
    /* Flash 1 MB on, through the 1:1 map, below /data at 2 MB.  Empty unless
     * a driver registers a fragment placing an output section .xip* here. */
    XIP (rx)   : ORIGIN = 0x42100000, LENGTH = 0xF0000
    /* PSRAM past the module window, once the boot has mapped it.  Empty
     * unless psram_data.ld places buffers here (TIKU_ESP32C61_PSRAM_DATA). */
    PSRAM (rw) : ORIGIN = 0x42808000, LENGTH = 0x1F8000
}

__sram_start = ORIGIN(SRAM);
__sram_end   = ORIGIN(SRAM) + LENGTH(SRAM);

SECTIONS
{
    /* The XIP and PSRAM fragments a build registered (TIKU_XIP_LDS,
     * TIKU_PSRAM_LDS), generated into the build directory; first, so their
     * patterns claim their sections. */
    INCLUDE tiku_xip.ld

    .text :
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
        /* Code a library marks as needing RAM, should XIP hold the rest
         * (the BLE controller's hot paths and the coexistence library's
         * always stay here). */
        *(.iram1 .iram1.*)
        *(.high_perf_code_iram1 .high_perf_code_iram1.*)
        *(.coexiram .coexiram.*)
        . = ALIGN(4);
    } > SRAM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        /* Persist-cell table: boot carries each cell by its key when an
         * update moves it (tiku_persist_move_boot, tiku_mem.h). */
        . = ALIGN(4);
        __tiku_cells_start = .;
        KEEP(*(.tiku_cells))
        __tiku_cells_end = .;
        . = ALIGN(4);
    } > SRAM

    /* End of code: /sys/mem/free reports image size from this. */
    _etext = .;

    .data :
    {
        . = ALIGN(4);
        __data_start = .;
        *(.data .data.*)
        *(.dram1 .dram1.*)
        /* gp sits 2 KB into the small data so one signed 12-bit offset
         * reaches all of it. */
        . = ALIGN(8);
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM

    /* Loaded whole, so load and run addresses agree and the startup copy
     * is a no-op kept for the day a stage copies the image from flash. */
    __data_load = LOADADDR(.data);
    __datastart = __data_start;

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SRAM

    /* Durable cells. Outside .bss so the zero-fill skips them and a warm
     * reset keeps them; the flash mirror carries them across power. */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        /* TIKU_DURABLE_FIRST: the layout record opens the durable image. */
        KEEP(*(.persistent.layout))
        *(.uninit*)
        *(.persistent)
        *(.retained)
        . = ALIGN(4);
        __uninit_end = .;
    } > SRAM

    _end = __uninit_end;
    PROVIDE(end = _end);

    __stack = __sram_end;

    /*
     * SRAM TIER -- what is left between the image and the stack, derived as
     * the other ports derive theirs, so it follows the image rather than a
     * constant chosen once.  The reserve is the stack's.
     */
    __tiku_tier_span_start      = ALIGN(__uninit_end, 32);
    __tiku_tier_span_limit      = __stack;
    __tiku_tier_reserve_default = 16K;
    __tiku_tier_floor_default   = 0x8000;
    INCLUDE arch/common/tiku_sram_layout.ld

    ASSERT((__uninit_end - __uninit_start) <= (4 * 4096 - 16),
           "durable .uninit exceeds the flash mirror; raise TIKU_FLASH_MIRROR_SECTORS in tiku_flash_arch.h AND the 4 in this ASSERT together")

    ASSERT((DEFINED(tiku_layout_pin) ? tiku_layout_pin : __uninit_start)
           == __uninit_start,
           "ERROR: tiku_layout_pin does not open .uninit (TIKU_DURABLE_FIRST holds one object)")
}
