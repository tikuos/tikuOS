/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * mod_demo_apollo4l.ld - loadable native module script (Apollo4 Lite / Plus).
 *
 * Position-FIXED at the carve VMA, so VMA == LMA and every internal reference
 * resolves at run address.  Layout and the no-writable-state rule below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The module is position-FIXED at the carve VMA (TIKU_MODULE_CARVE_ADDR): the
 * loader programs the flat image there via the bootrom and jumps in, so
 * VMA == LMA == the carve base and every internal reference resolves at run
 * address.  Layout:
 *   +0    .modhdr   (tiku_module_header_t, exactly 16 bytes)
 *   +16   .modinit  (module_init entry -- header's init_off points here)
 *   ...   .text / .rodata
 * No .data/.bss: the MVP module holds no writable state (pure handlers), so a
 * missing RAM/copy step cannot bite.  Keep in sync with the header + the
 * apollo4l/apollo4p code cap (slot = cap - 32 KB).
 */

MEMORY
{
    /* Executable MRAM slot: the top 32 KB of the code window (cap 0x78000
     * less the module reserve) -- MRAM is executable but only
     * bootrom-programmable, so the loader installs via
     * tiku_nvm_mram_program instead of CPU stores.  Kept in sync with
     * TIKU_MODULE_CARVE_ADDR. */
    CARVE (rx) : ORIGIN = 0x70000, LENGTH = 0x8000   /* 32 KB */
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
     * Writable module state is NOT SUPPORTED, and saying so at link time is
     * the point of this section.  Modules run execute-in-place from NVM and
     * are never copied to RAM: there is no load step, no .data initialiser
     * copy, and the syscall jump table exposes no allocator.  These inputs
     * would be silently dropped by a /DISCARD/ entry, so a module declaring a
     * global would be accepted and shipped with its state pointing at nothing.
     * Collect them instead and require the result to be empty, so the mistake
     * is a link error naming the cause.  --gc-sections still drops a global
     * that nothing references, so only state a handler actually uses trips it.
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
