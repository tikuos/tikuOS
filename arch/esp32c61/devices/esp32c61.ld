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
 * Flash is storage, and a code window for the sections an XIP fragment
 * places there; `make` writes that part as xip.bin.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Memory map (HP SRAM, 320 KB at 0x40800000, one space for code and data):
 *   0x40800000  .text, .rodata, .data, .bss, .uninit, then the SRAM tier
 *   0x4084C000  __stack, growing down into a reserve the tier never takes
 *   0x4084C670  to the top: the ROM's stack and data, which its delay and
 *               flash routines use; the image does not touch it
 *
 * Symbols the kernel expects:
 *   __stack, _end, _etext, __data_load, __data_start, __data_end,
 *   __bss_start, __bss_end, __sram_start, __sram_end, __uninit_start/end
 */

ENTRY(tiku_esp32c61_reset_handler)

MEMORY
{
    SRAM (rwx) : ORIGIN = 0x40800000, LENGTH = 0x4C000
    /* Flash from offset 1 MB, through the 1:1 map, below /data at 2 MB.
     * Empty unless a registered fragment places an .xip* section here. */
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
    .xip.end : { __tiku_xip_end = .; } > XIP
    __tiku_xip_start = ORIGIN(XIP);

    .text :
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
        /* Code a library marks as needing RAM, such as the BLE controller's
         * hot paths and the coexistence library, stays in SRAM when XIP
         * holds the rest. */
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
        /* gp sits 2 KB into the small data, so a signed 12-bit offset from
         * gp reaches its first 4 KB. */
        . = ALIGN(8);
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM

    /* The ROM loads the image whole, so .data's load and run addresses
     * agree and the startup copy is skipped. */
    __data_load = LOADADDR(.data);
    /* Flash payload loaded into SRAM by the ROM; excludes image headers. */
    __tiku_flash_payload_bytes = SIZEOF(.text) + SIZEOF(.rodata) +
                                 SIZEOF(.data);
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
     * SRAM tier: the span between the end of the image and __stack, which
     * moves with the image.  The reserve below __stack is the stack's.
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
