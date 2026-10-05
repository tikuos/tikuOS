/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * mod_demo_rp2350.ld - loadable native module script (RP2350).
 *
 * Links the module at its flash slot, so VMA == LMA and every internal
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
    /* XIP flash slot: the top 32 KB (8 erase sectors) of the code window.
     * Kept in sync with TIKU_MODULE_CARVE_ADDR. */
    CARVE (rx) : ORIGIN = 0x10058000, LENGTH = 0x8000   /* 32 KB */
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
     * .bss, and the service table has no allocator.  A /DISCARD/ entry would
     * drop these inputs silently and ship a global pointing at nothing, so
     * they are collected here and required to be empty, which makes writable
     * state a link error naming the cause.  --gc-sections still drops a
     * global that nothing references, so only state a handler uses trips it.
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
