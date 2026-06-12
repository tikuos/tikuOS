/*
 * Tiku Operating System v0.05 — Ambiq Apollo 510 (Cortex-M55) linker script
 *
 * Memory map (AmbiqSuite am_reg_base_addresses.h):
 *   ITCM   : 256 KB at 0x00000000   (not used at bring-up — see .text)
 *   MRAM   : 4 MB   at 0x00400000   (flash; low 64 KB reserved for the SBL)
 *   DTCM   : 512 KB at 0x20000000   (.data / .bss / heap / stack)
 *   SSRAM  : 3 MB   at 0x20080000   (shared SRAM — unused at bring-up)
 *
 * Unlike the RP2350 port there is NO .boot2 / .image_def header: the
 * on-silicon Secure Boot Loader (resident in MRAM 0x400000..0x410000)
 * transfers control straight to the application vector table at the
 * start of user MRAM (0x410000), loading the initial SP and reset PC
 * from it the standard Cortex-M way.
 *
 * Symbols exposed to the kernel match the RP2350 port so the shared
 * startup / memory / region code is identical:
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
    MRAM  (rx)  : ORIGIN = 0x00410000, LENGTH = 4128768 - 0x8000  /* ~3.9 MB; top 32 KB reserved below for the NVM mirror (mem port C) */
    TCM   (rwx) : ORIGIN = 0x20000000, LENGTH = 512K      /* DTCM: .data/.bss/heap/stack    */
    SSRAM (rwx) : ORIGIN = 0x20080000, LENGTH = 3M        /* shared SRAM (unused at bring-up) */
}

__flash_start = ORIGIN(MRAM);
__flash_end   = ORIGIN(MRAM) + LENGTH(MRAM);

/* NVM mirror page (mem port C): 32 KB reserved just above the shrunk MRAM
 * region (0x7F8000..0x800000), where tiku_mem_arch_nvm_flush() programs the
 * .uninit snapshot for power-cycle durability. Far above the firmware code; the
 * SBL (0x400000) is never touched. */
__tiku_nvm_mram_start = ORIGIN(MRAM) + LENGTH(MRAM);   /* 0x7F8000 */
__tiku_nvm_mram_size  = 0x8000;                        /* 32 KB */
__sram_start  = ORIGIN(TCM);
__sram_end    = ORIGIN(TCM) + LENGTH(TCM);

/* The Cortex-M loads the initial SP from vector entry 0. Place it at
 * the very top of DTCM and let the stack grow downward toward .bss. */
__stack       = __sram_end;

SECTIONS
{
    /* -------- 1. Vector table (at user-MRAM origin 0x410000) ---------- */
    /* Apollo510 requires 512-byte vector-table alignment (>=49 external
     * IRQs); 0x410000 already satisfies it. The SBL loads SP+reset PC
     * from here, and the reset handler also writes VTOR explicitly. */
    .vectors ORIGIN(MRAM) : ALIGN(512)
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        __vectors_end = .;
    } > MRAM

    /* AmbiqSuite's SystemInit() (system_apollo510.c) sets VTOR from the
     * symbol __Vectors. We own the vector table under a different name, so
     * alias it here to let the SDK file link. Our reset handler also writes
     * VTOR, so this is belt-and-braces. */
    __Vectors = tiku_ambiq_vectors;

    /* -------- 2. Code & rodata --------------------------------------- */
    .text : ALIGN(4)
    {
        *(.text*)
        *(.glue_7) *(.glue_7t)
        /* AmbiqSuite marks some HAL functions for ITCM. At bring-up we
         * run them in place from MRAM (position-independent Thumb)
         * rather than copying to ITCM. Revisit for performance later. */
        *(.itcm_text*)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > MRAM

    /* End of .text. tikuOS's `free`/ /sys/mem/free use _etext to size the
     * FRAM-resident (here MRAM-resident) code region. Standard newlib name;
     * `etext` aliased for code that uses the historic spelling. */
    _etext = .;
    etext  = .;

    .rodata : ALIGN(4)
    {
        *(.rodata*)
        *(.gnu.linkonce.r.*)
    } > MRAM

    .ARM.extab : { *(.ARM.extab*) } > MRAM
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > MRAM

    /* -------- 3. Initialised data (LMA in MRAM, VMA in DTCM) ---------- */
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
    /* The MemManage/HardFault handler records the violation here. After .bss
     * (the crt zero-loop stops at __bss_end, so this survives) and before
     * .uninit (so it lands in the SRAM_LO RW region, not the RO NVM region);
     * outside .uninit so it is NOT mirrored to MRAM. */
    .mpu_diag (NOLOAD) : ALIGN(32)
    {
        __mpu_diag_start = .;
        *(.mpu_diag*)
        . = ALIGN(32);
        __mpu_diag_end = .;
    } > TCM

    /* -------- 5. Persistent (skipped by zero-init) ------------------- */
    /* Mirrors the RP2350 .uninit model: survives warm reset, reseeded
     * on power cycle. ALIGN(32) for the ARMv8-M MPU region base.
     * (MRAM-backed NVM-mirror symbols arrive in Phase 2 with
     * tiku_mem_arch.c.) */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        *(.uninit*)
        *(.persistent*)
        . = ALIGN(32);
        __uninit_end = .;
    } > TCM

    /* End of static allocations / start of the _sbrk heap. Aliases for
     * whichever name the linked code uses (tikuOS, newlib, libgloss). */
    _end = .;
    end = .;
    __end__ = .;
    __datastart = __data_start;

    /* Shared SRAM (3 MB at 0x20080000), powered in tiku_crt_early.c. Holds the
     * large volatile tier backing buffers (.ssram), zero-initialised by the
     * reset handler after the SSRAM is powered. ALIGN(32) for an MPU region
     * base (mem port D). */
    .ssram (NOLOAD) : ALIGN(32)
    {
        __ssram_start = .;
        *(.ssram*)
        . = ALIGN(32);
        __ssram_end = .;
    } > SSRAM

    /* NOLOAD catch so any Ambiq HAL .shared / .resource_table symbols
     * resolve cleanly. */
    .shared (NOLOAD) :
    {
        KEEP(*(.resource_table))
        *(.shared*)
    } > SSRAM

    /* Sanity: leave >=4 KB between end-of-statics and the stack top. */
    ASSERT((__stack - _end) >= 0x1000,
           "ERROR: DTCM exhausted — less than 4 KB free for stack")

    /* The NVM mirror page must hold the magic word + the whole .uninit image. */
    ASSERT((__uninit_end - __uninit_start) <= (__tiku_nvm_mram_size - 4),
           "ERROR: .uninit too large for the 32 KB NVM mirror page (mem port C)")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
