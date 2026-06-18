/*
 * Tiku Operating System v0.05 - Ambiq Apollo4 Lite (Cortex-M4F) linker script
 *
 * Memory map (AmbiqSuite R4.5.0 apollo4l_evb):
 *   MRAM   : 2 MB   based at 0x0       (flash; low 96 KB reserved for boot/info)
 *   TCM    : 384 KB at 0x10000000      (.data / .bss / heap / stack)
 *   SSRAM  : 1 MB   at 0x10060000      (shared SRAM, contiguous after TCM)
 *
 * The application image (vector table first) lives at user MRAM 0x00018000;
 * the boot ROM transfers control there the standard Cortex-M way (initial SP
 * and reset PC from the vector table). Apollo4 Lite is not a secure part, so
 * there is no SBL halt-after-reset (unlike Apollo510).
 *
 * Symbols exposed to the kernel match the Apollo510 / RP2350 ports so the
 * shared startup / memory / region code is identical:
 *   __stack, __sram_start/end, __flash_start/end,
 *   __data_load/start/end, __bss_start/end, __uninit_start/end,
 *   _end / end / __end__ / __datastart
 *
 * SPDX-License-Identifier: Apache-2.0
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(tiku_ambiq_reset_handler)

MEMORY
{
    MRAM  (rx)  : ORIGIN = 0x00018000, LENGTH = 1998848 - 0x8000  /* ~1.9 MB; top 32 KB reserved below for the NVM mirror */
    TCM   (rwx) : ORIGIN = 0x10000000, LENGTH = 384K      /* .data/.bss/heap/stack          */
    SSRAM (rwx) : ORIGIN = 0x10060000, LENGTH = 1M        /* shared SRAM (unused at bring-up) */
}

__flash_start = ORIGIN(MRAM);
__flash_end   = ORIGIN(MRAM) + LENGTH(MRAM);

/* NVM mirror page: 32 KB reserved just above the shrunk MRAM region
 * (0x1F8000..0x200000), where tiku_mem_arch_nvm_flush() programs the .uninit
 * snapshot for power-cycle durability (full-kernel milestone). */
__tiku_nvm_mram_start = ORIGIN(MRAM) + LENGTH(MRAM);   /* 0x1F8000 */
__tiku_nvm_mram_size  = 0x8000;                        /* 32 KB */
__sram_start  = ORIGIN(TCM);
__sram_end    = ORIGIN(TCM) + LENGTH(TCM);

/* The Cortex-M loads the initial SP from vector entry 0. Place it at the very
 * top of TCM and let the stack grow downward toward .bss. */
__stack       = __sram_end;

SECTIONS
{
    /* -------- 1. Vector table (at user-MRAM origin 0x18000) ----------- */
    /* Apollo4 Lite has 84 external IRQs, so the table needs >=400 bytes ->
     * 512-byte VTOR alignment; 0x18000 already satisfies it. The reset
     * handler also writes VTOR explicitly. */
    .vectors ORIGIN(MRAM) : ALIGN(512)
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        __vectors_end = .;
    } > MRAM

    /* SystemInit()-style code may set VTOR from __Vectors. We own the table
     * under tiku_ambiq_vectors; alias it. The reset handler also writes VTOR. */
    __Vectors = tiku_ambiq_vectors;

    /* -------- 2. Code & rodata --------------------------------------- */
    .text : ALIGN(4)
    {
        *(.text*)
        *(.glue_7) *(.glue_7t)
        *(.itcm_text*)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > MRAM

    _etext = .;
    etext  = .;

    .rodata : ALIGN(4)
    {
        *(.rodata*)
        *(.gnu.linkonce.r.*)
    } > MRAM

    .ARM.extab : { *(.ARM.extab*) } > MRAM
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > MRAM

    /* -------- 3. Initialised data (LMA in MRAM, VMA in TCM) ----------- */
    __data_load = LOADADDR(.data);
    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > TCM AT > MRAM

    /* -------- 4. Zero-initialised BSS -------------------------------- */
    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > TCM

    /* -------- 4b. MPU diagnostics (NOLOAD, warm-reset durable) -------- */
    .mpu_diag (NOLOAD) : ALIGN(32)
    {
        __mpu_diag_start = .;
        *(.mpu_diag*)
        . = ALIGN(32);
        __mpu_diag_end = .;
    } > TCM

    /* -------- 5. Persistent (skipped by zero-init) ------------------- */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        *(.uninit*)
        *(.persistent*)
        . = ALIGN(32);
        __uninit_end = .;
    } > TCM

    _end = .;
    end = .;
    __end__ = .;
    __datastart = __data_start;

    /* Shared SRAM (1 MB at 0x10060000). Holds the large volatile tier backing
     * buffers (.ssram), zero-initialised by the reset handler. ALIGN(32) for a
     * PMSAv7 MPU region base. */
    .ssram (NOLOAD) : ALIGN(32)
    {
        __ssram_start = .;
        *(.ssram*)
        . = ALIGN(32);
        __ssram_end = .;
    } > SSRAM

    .shared (NOLOAD) :
    {
        KEEP(*(.resource_table))
        *(.shared*)
    } > SSRAM

    /* Sanity: leave >=4 KB between end-of-statics and the stack top. */
    ASSERT((__stack - _end) >= 0x1000,
           "ERROR: TCM exhausted -- less than 4 KB free for stack")

    /* The NVM mirror page must hold the magic word + the whole .uninit image. */
    ASSERT((__uninit_end - __uninit_start) <= (__tiku_nvm_mram_size - 4),
           "ERROR: .uninit too large for the 32 KB NVM mirror page")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
