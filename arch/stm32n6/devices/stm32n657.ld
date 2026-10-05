/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * stm32n657.ld - STM32N657 (NUCLEO-N657X0-Q) linker script.
 *
 * The part has no internal flash: the boot ROM loads one signed image into
 * an SRAM window and jumps to it, so code, data and stack live in that window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Memory map:
 *   0x34180400  image base, 255 KB  -- the window the boot ROM loads an FSBL
 *                                      into, per the CubeProgrammer device DB
 *                                      (Embedded SRAM, 0x3FC00 bytes, RWE)
 *   0x341C0000  AXI SRAM, 2 MB      -- .axisram, then the SRAM tier span
 *
 * Layout in the image window:
 *   0x34180400  .vectors    Cortex-M55 table, 704 bytes, 1 KB aligned for
 *                           VTOR; the ROM jumps to the entry in word 1 and
 *                           does not load SP from word 0
 *   0x341806c0  .text/.rodata
 *               .data       load address equals run address: the image is
 *                           loaded whole, so the startup copy is a no-op
 *               .bss        zeroed by the reset handler
 *               .uninit     durable cells, not zeroed
 *   0x341C0000  __stack     top of the window, growing down toward _end
 *
 * Symbols the kernel expects:
 *   __stack, _end, __data_load, __data_start, __data_end,
 *   __bss_start, __bss_end, __sram_start, __sram_end
 */

ENTRY(tiku_stm32n6_reset_handler)

MEMORY
{
    SRAM   (rwx) : ORIGIN = 0x34180400, LENGTH = 0x3FC00
    AXISRAM (rw) : ORIGIN = 0x341C0000, LENGTH = 0x200000
}

__sram_start = ORIGIN(SRAM);
__sram_end   = ORIGIN(SRAM) + LENGTH(SRAM);

SECTIONS
{
    .vectors ORIGIN(SRAM) :
    {
        KEEP(*(.vectors))
    } > SRAM

    .text :
    {
        . = ALIGN(4);
        *(.text .text.*)
        *(.rodata .rodata.*)
        /* Persist-cell table: boot carries each cell by its key when an
         * update moves it (tiku_persist_move_boot, tiku_mem.h). */
        . = ALIGN(4);
        __tiku_cells_start = .;
        KEEP(*(.tiku_cells))
        __tiku_cells_end = .;
        *(.glue_7) *(.glue_7t)
        . = ALIGN(4);
    } > SRAM

    /* ARM unwind tables, bounded by __exidx_start and __exidx_end for the
     * libgcc unwinder. */
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > SRAM

    /* End of code and read-only data; the `free` command reads it. */
    _etext = .;

    .data :
    {
        . = ALIGN(4);
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM

    __data_load = LOADADDR(.data);
    __datastart = __data_start;

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SRAM

    /* Durable cells, outside .bss so the reset handler does not zero them.
     * The boot ROM clears SRAM on a system reset; tiku_mem_arch.c mirrors
     * this span to the NOR on each flush and restores it at boot. */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        /* TIKU_DURABLE_FIRST: the layout record opens the mirrored image, at
         * an offset every image shares; the rest follow link order. */
        KEEP(*(.persistent.layout))
        *(.uninit*)
        *(.persistent)
        /* Retained state is mirrored here too; moving it changes the saved
         * offsets. */
        *(.retained)
        . = ALIGN(4);
        __uninit_end = .;
    } > SRAM

    /* The AXI SRAM above __stack, past the image window the boot ROM loads.
     * NOLOAD: the ROM copies the image only.  tiku_stm32n6_sram_init() must
     * power the banks before the startup code zeroes this section. */
    .axisram (NOLOAD) : ALIGN(32)
    {
        __axisram_start = .;
        *(.axisram*)
        . = ALIGN(4);
        __axisram_end = .;
        . = ALIGN(32);
        __axisram_top = .;
    } > AXISRAM

    /*
     * SRAM tier: the rest of the AXI SRAM above .axisram, less a 32 KB
     * reserve.  The span lies outside every output section, so the startup
     * code does not zero it, and it moves up as .axisram grows.
     *
     * The reserve guards nothing here: the stack is in the image window and
     * nothing sits above the tier in this bank.  A smaller reserve gives the
     * tier the difference.
     */
    __tiku_tier_span_start      = __axisram_top;
    __tiku_tier_span_limit      = ORIGIN(AXISRAM) + LENGTH(AXISRAM);
    __tiku_tier_reserve_default = 32K;
    __tiku_tier_floor_default   = 0x40000;
    INCLUDE arch/common/tiku_sram_layout.ld

    _end = __uninit_end;
    PROVIDE(end = _end);

    __stack = __sram_end;

    /* The durable region is mirrored into four 4 KB NOR sectors less a
     * 16-byte header; a larger region fails the link.
     *
     * The 4 here must equal TIKU_XSPI_MIRROR_SECTORS in tiku_xspi_arch.h,
     * which the linker cannot read.  With only this bound raised, a larger
     * region links and tiku_mem_arch_nvm_flush_status() refuses to mirror
     * any of it. */
    ASSERT((__uninit_end - __uninit_start) <= (4 * 4096 - 16),
           "durable .uninit exceeds the NOR mirror; raise TIKU_XSPI_MIRROR_SECTORS in tiku_xspi_arch.h AND the 4 in this ASSERT together")

    /* The layout record opens the durable image in every build, so images
     * with different durable variables find it at the same offset. */
    ASSERT((DEFINED(tiku_layout_pin) ? tiku_layout_pin : __uninit_start)
           == __uninit_start,
           "ERROR: tiku_layout_pin does not open .uninit (TIKU_DURABLE_FIRST holds one object)")

    /* The array ends at 0x343C0000.  An access above it hangs the bus with
     * no fault and no reset, so .axisram past that address fails the link. */
    ASSERT(__axisram_end <= 0x343C0000,
           "tier arena runs past the top of AXI SRAM; shrink the .axisram statics")

    /DISCARD/ :
    {
        *(.comment)
        *(.ARM.attributes)
    }
}
