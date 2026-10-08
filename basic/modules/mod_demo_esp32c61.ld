/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * mod_demo_esp32c61.ld - loadable native module script (ESP32-C61).
 *
 * Links the module at its PSRAM execution window, so VMA == LMA and every
 * internal reference resolves at run address.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

MEMORY
{
    /* 32 KB at the base of the in-package PSRAM, which the PSRAM tier leaves
     * out when the loader is built in.  The image lives in the store as an
     * ordinary file and is copied here at activate.  It must equal
     * TIKU_MODULE_EXEC_ADDR and TIKU_ESP32C61_MODULE_WINDOW. */
    CARVE (rx) : ORIGIN = 0x42800000, LENGTH = 0x8000
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
        *(.srodata*)
        . = ALIGN(4);
    } > CARVE

    ASSERT(SIZEOF(.mod) <= LENGTH(CARVE), "module image overflows the carve")

    /* Modules get no RAM: the .data, .bss and small-data inputs are collected
     * here and must be empty, so writable state fails the link with a message
     * naming the cause.  The module build passes -msmall-data-limit=0. */
    .modwritable (NOLOAD) : { *(.bss*) *(.data*) *(.sbss*) *(.sdata*) *(COMMON) }

    ASSERT(SIZEOF(.modwritable) == 0,
           "module declares writable state (.data/.bss) -- modules get no RAM; keep state on the caller's stack or hand it to the firmware")

    /DISCARD/ :
    {
        *(.riscv.attributes)
        *(.eh_frame*)
        *(.comment)
        *(.note*)
        *(.debug*)
    }
}
