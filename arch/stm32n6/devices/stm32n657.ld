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
 * SRAM and jumps to it, so code, data and stack all live in that window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Memory map:
 *   0x34180400  image base, 255 KB  -- the window the boot ROM loads an FSBL
 *                                      into, per the CubeProgrammer device DB
 *                                      (Embedded SRAM, 0x3FC00 bytes, RWE)
 *
 * Layout in that window:
 *   0x34180400  .vectors    Cortex-M55 table; the ROM reads word 0 as the
 *                           initial SP and word 1 as the entry point, so it
 *                           must come first and stay 1 KB aligned
 *   0x341804c0  .text/.rodata
 *               .data       load address equals run address: the image is
 *                           loaded whole, so the startup copy is a no-op
 *               .bss        zeroed by the reset handler
 *   0x341C0000  __stack     top of the window, growing down toward .bss
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
        *(.glue_7) *(.glue_7t)
        . = ALIGN(4);
    } > SRAM

    /* ARM unwind tables: kept so a fault handler can walk the stack. */
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > SRAM

    /* End of .text: /sys/mem/free reports image size from this. */
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

    /* Durable cells. Outside .bss so the reset handler's zero-fill skips them,
     * which is what lets a value survive a warm reset. Nothing here survives
     * power loss on this part: the whole image is SRAM until an XSPI backend
     * exists, so this is warm-reset persistence only. */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        *(.uninit*)
        *(.persistent)
        *(.persistent.warm)
        . = ALIGN(4);
        __uninit_end = .;
    } > SRAM

    /* The AXI SRAM array runs on past the image window the boot ROM loads
     * into, so the tier arena lives above __stack rather than competing with
     * .bss for the 255 KB the ROM copies. NOLOAD: the ROM copies the image
     * only, and tiku_stm32n6_sram_init() has to wake the banks before the
     * startup code may zero this. */
    .axisram (NOLOAD) : ALIGN(32)
    {
        __axisram_start = .;
        *(.axisram*)
        . = ALIGN(4);
        __axisram_end = .;
    } > AXISRAM

    _end = __uninit_end;
    PROVIDE(end = _end);

    __stack = __sram_end;

    /* The durable region is mirrored into four 4 KB NOR sectors less a
     * 16-byte header. A region that outgrows that would lose its tail at
     * runtime with nothing to show for it, so it is a link error instead. */
    ASSERT((__uninit_end - __uninit_start) <= (4 * 4096 - 16),
           "durable .uninit exceeds the NOR mirror; raise TIKU_XSPI_MIRROR_SECTORS")

    /* The array ends at 0x343C0000 -- measured, not assumed: a write above it
     * bus-faults rather than wrapping, so an oversized arena would fail as a
     * boot-time wedge instead of a link error. */
    ASSERT(__axisram_end <= 0x343C0000,
           "tier arena runs past the top of AXI SRAM; lower TIKU_TIER_SRAM_SIZE")

    /DISCARD/ :
    {
        *(.comment)
        *(.ARM.attributes)
    }
}
