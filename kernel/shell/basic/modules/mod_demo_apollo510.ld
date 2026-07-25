/*
 * Tiku Operating System v0.05 -- loadable native module linker script
 * (Apollo510/510b variant).
 *
 * The module is position-FIXED at the execution window (TIKU_MODULE_EXEC_ADDR
 * -- the ITCM on this part): the loader copies the flat image there and jumps
 * in, so VMA == LMA == the window base and every internal reference resolves at
 * run address.  Layout:
 *   +0    .modhdr   (tiku_module_header_t, exactly 16 bytes)
 *   +16   .modinit  (module_init entry -- header's init_off points here)
 *   ...   .text / .rodata
 * No .data/.bss: the MVP module holds no writable state (pure handlers), so a
 * missing RAM/copy step cannot bite.  Keep in sync with the header.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

MEMORY
{
    /* 32 KB execution window in the ITCM, 4 KB in from its base (so no module
     * address is ever zero).  The image lives in the store as an ordinary file
     * and is copied here at activate time -- there is no NVM slot on this part
     * any more, which is why this used to say MRAM.  Kept in sync with
     * TIKU_MODULE_EXEC_ADDR. */
    CARVE (rx) : ORIGIN = 0x00001000, LENGTH = 0x8000  /* ITCM window */
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
     * used to be listed in /DISCARD/ below, which meant a module declaring a
     * global was accepted and shipped with its state pointing at nothing.
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
