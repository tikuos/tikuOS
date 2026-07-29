/*
 * Tiku Operating System v0.06 -- loadable native module linker script.
 *
 * The module is position-FIXED at the carve VMA (TIKU_MODULE_CARVE_ADDR): the
 * loader copies the flat image there and jumps in, so VMA == LMA == the carve
 * base and every internal reference resolves at run address.  Layout:
 *   +0    .modhdr   (tiku_module_header_t, exactly 16 bytes)
 *   +16   .modinit  (module_init entry -- header's init_off points here)
 *   ...   .text / .rodata
 * No .data/.bss: the MVP module holds no writable state (pure handlers), so a
 * missing RAM/copy step cannot bite.  Keep in sync with the header + device .ld.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

MEMORY
{
    /* FRAM slot at the top of HIFRAM (R+W+X MPU segment); ends shy of CPU47.
     * Kept in sync with __tiku_module_slot / TIKU_MODULE_CARVE_ADDR. */
    CARVE (rx) : ORIGIN = 0x43000, LENGTH = 0xFF0
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
        *(.MSP430.attributes*)
        *(.comment)
        *(.note*)
        *(.debug*)
    }
}
