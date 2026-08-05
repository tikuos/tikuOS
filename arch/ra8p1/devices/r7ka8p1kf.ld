/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * r7ka8p1kf.ld - R7KA8P1KF (EK-RA8P1) linker script, SRAM-resident image.
 *
 * The 1 MB code MRAM is not written until R6 establishes how to do it
 * safely, so code, data and stack all live in the SRAM window at 0x22000000.
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

    /*
     * ALIGN(32) on every MPU boundary, not ALIGN(4).
     *
     * PMSAv8's region granule is 32 bytes: RBAR keeps base[31:5] and RLAR
     * keeps limit[31:5].  A boundary that is only 4-aligned cannot be
     * expressed, so the region would either overlap its neighbour (behaviour
     * the architecture leaves implementation-defined) or leave a hole that
     * falls through to the background map.  Aligning here is what lets the
     * W^X split be exact rather than approximate.
     */
    .text : ALIGN(32)
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(32);
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

    /*
     * No AT() here, and __data_load comes from LOADADDR AFTERWARDS.
     *
     * Capturing `.` into __data_load and pinning the LMA to it is wrong the
     * moment the linker pads: .data aligns to 8, so if `.` is only 4-aligned
     * the VMA moves up while the LMA does not.  The loader writes the VMA,
     * the startup copy then sees src != dst and shifts every initialised
     * variable down by the padding -- .data silently corrupt, and only in
     * builds whose alignment happened not to work out.  LOADADDR reports what
     * the linker actually chose, so src == dst and the copy is correctly
     * skipped.
     */
    .data : ALIGN(32)
    {
        __data_start = .;
        __datastart = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM

    __data_load = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(32)
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
        /* DURABLE grade: exact-match `.persistent` only.  The MPU's NVM
         * region covers __uninit_start..__uninit_end and nothing else. */
        *(.persistent)
        . = ALIGN(32);
        __uninit_end = .;
        /* WARM grade sits OUTSIDE that window, deliberately.  tiku_mem.h is
         * explicit that TIKU_PERSIST_WARM is "never mirrored, never
         * MPU-protected", and its callers write it unbracketed on purpose --
         * tiku_hang_boot_init() clears the hang record at every boot.  Sweep
         * it in with `.persistent` and that write MemManage-faults before the
         * shell ever starts, which is how this was found. */
        *(.persistent.warm*)
        . = ALIGN(32);
    } > SRAM

    _end = __uninit_end;
    PROVIDE(end = _end);

    __stack = ORIGIN(SRAM) + LENGTH(SRAM);

    /* Stack headroom.  The SRAM window is large, so this is a tripwire for a
     * runaway static allocation rather than a tight budget. */
    ASSERT((__stack - _end) >= 0x4000,
           "ERROR: RA8P1 SRAM exhausted -- less than 16 KB left for stack")
}
