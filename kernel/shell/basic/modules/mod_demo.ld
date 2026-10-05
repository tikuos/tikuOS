/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * mod_demo.ld - loadable native module script (nRF54L15 / nRF54LM20).
 *
 * Links the module at its RRAM slot, so VMA == LMA and every internal
 * reference resolves at run address.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Layout:
 *   +0    .modhdr   (tiku_module_header_t, 16 bytes)
 *   +16   .modinit  (module_init; the header's init_off points here)
 *   ...   .text / .rodata
 * No .data or .bss: a module holds no writable state (see .modwritable).
 */

MEMORY
{
    /* RRAM slot at the top of the 384 KB Nordic code window, at the same
     * address on nRF54L15 and nRF54LM20, so one image serves both (SRAM is
     * execute-never there).  It must equal TIKU_MODULE_CARVE_ADDR. */
    CARVE (rx) : ORIGIN = 0x58000, LENGTH = 0x8000   /* 32 KB */
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

    /*
     * Modules get no data RAM: nothing copies a .data initialiser or clears a
     * .bss, and the service table has no allocator.  The .data and .bss
     * inputs are collected here and must be empty, so writable state fails
     * the link with a message naming the cause.  --gc-sections drops an
     * unreferenced global first, so only state a handler uses fails it.
     */
    .modwritable (NOLOAD) : { *(.bss*) *(.data*) *(COMMON) }

    ASSERT(SIZEOF(.modwritable) == 0,
           "module declares writable state (.data/.bss) -- modules get no RAM; keep state on the caller's stack or hand it to the firmware")

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note*)
        *(.debug*)
    }
}
