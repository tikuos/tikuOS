/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * r7ka8p1kf.ld - R7KA8P1KF (EK-RA8P1) linker script, SRAM-resident image.
 *
 * The part has 1 MB of code MRAM at 0x02000000, but R2 images are loaded into
 * SRAM by the debugger: nothing is programmed into MRAM until R6/R7 establish
 * how to write it safely.  So code, data and stack all live in the SRAM
 * window, exactly as the STM32N6 port does for the same reason.
 *
 * Memory map (MEASURED on this board, R1 log):
 *   0x22000000  user SRAM.  Read/write verified to 0x221C0000; the datasheet
 *               puts user SRAM at 1664 KB, ending 0x221A0000.  The smaller
 *               figure is used -- the port does not rely on the 128 KB the
 *               two disagree about.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

ENTRY(tiku_ra8p1_reset_handler)

MEMORY
{
    SRAM (rwx) : ORIGIN = 0x22000000, LENGTH = 1664K
}

SECTIONS
{
    .vectors : ALIGN(512)
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        __vectors_end = .;
    } > SRAM

    .text :
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > SRAM

    /* ARM unwind tables: kept so a fault handler can walk the stack.  Without
     * an explicit output section the linker gives .ARM.exidx its own default
     * placement and it collides with .data's load address -- a link error, but
     * an obscure one to read. */
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > SRAM

    _etext = .;

    __data_load = .;
    .data : AT(__data_load)
    {
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM

    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SRAM

    /* Durable cells sit outside .bss so the reset handler's zero-fill skips
     * them, which is what lets a value survive a warm reset.  Surviving power
     * loss needs the MRAM mirror, which is R6's milestone -- until then this
     * region is warm-reset durable only, and the port claims nothing more. */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        *(.uninit*)
        *(.persistent)
        *(.persistent.warm)
        . = ALIGN(4);
        __uninit_end = .;
    } > SRAM

    _end = __uninit_end;
    PROVIDE(end = _end);

    __stack = ORIGIN(SRAM) + LENGTH(SRAM);

    /* Stack headroom.  The SRAM window is large, so this is a tripwire for a
     * runaway static allocation rather than a tight budget. */
    ASSERT((__stack - _end) >= 0x4000,
           "ERROR: RA8P1 SRAM exhausted -- less than 16 KB left for stack")
}
