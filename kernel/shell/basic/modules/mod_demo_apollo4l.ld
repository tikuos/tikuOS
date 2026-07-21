/*
 * Tiku Operating System v0.05 -- loadable native module linker script
 * (Apollo4 Lite / Plus variant; both parts share the MRAM window layout).
 *
 * The module is position-FIXED at the carve VMA (TIKU_MODULE_CARVE_ADDR): the
 * loader programs the flat image there via the bootrom and jumps in, so
 * VMA == LMA == the carve base and every internal reference resolves at run
 * address.  Layout:
 *   +0    .modhdr   (tiku_module_header_t, exactly 16 bytes)
 *   +16   .modinit  (module_init entry -- header's init_off points here)
 *   ...   .text / .rodata
 * No .data/.bss: the MVP module holds no writable state (pure handlers), so a
 * missing RAM/copy step cannot bite.  Keep in sync with the header + the
 * apollo4l/apollo4p device .ld (__tiku_module_slot).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

MEMORY
{
    /* Executable MRAM slot: 4 KB at the top of the (shrunk) code window --
     * MRAM is executable but only bootrom-programmable, so the loader
     * installs via tiku_nvm_mram_program instead of CPU stores.  Kept in
     * sync with __tiku_module_slot / TIKU_MODULE_CARVE_ADDR. */
    CARVE (rx) : ORIGIN = 0x97000, LENGTH = 0x1000    /* 4 KB */
}

ENTRY(module_init)

SECTIONS
{
    .mod ORIGIN(CARVE) :
    {
        KEEP(*(.modhdr))
        . = ORIGIN(CARVE) + 16;      /* module_init lands exactly at +16 */
        KEEP(*(.modinit))
        *(.text*)
        *(.rodata*)
        *(.rodata.*)
        . = ALIGN(4);
    } > CARVE

    ASSERT(SIZEOF(.mod) <= LENGTH(CARVE), "module image overflows the carve")

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note*)
        *(.debug*)
        *(.bss*)
        *(.data*)
    }
}
