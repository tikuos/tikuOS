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
    /* Code window: a fixed 384 KB budget for code/rodata -- the fleet-wide
     * contract, sized for code growth (largest measured image 130.3 KB).
     * apollo510 carves NO module slot (its module executes from ITCM), so
     * everything between this window and the 64 KB durable mirror becomes the
     * carved NVM region -- ~3.5 MB, which the tier and /data split. Overflow
     * here means code outgrew its budget -- do NOT raise it to fit data. */
    MRAM  (rx)  : ORIGIN = 0x00410000, LENGTH = 0x60000   /* 384 KB code window (M4) */
    TCM   (rwx) : ORIGIN = 0x20000000, LENGTH = 512K      /* DTCM: .data/.bss/heap/stack    */
    SSRAM (rwx) : ORIGIN = 0x20080000, LENGTH = 3M        /* shared SRAM (unused at bring-up) */
}

__flash_start = ORIGIN(MRAM);
__flash_end   = ORIGIN(MRAM) + LENGTH(MRAM);

/* NO loadable-native-module slot on this part (module_size = 0). A Tier-3
 * module image is a store file (mod.bin) that the loader copies into the ITCM
 * and runs from there -- see tiku_basic_module.h and modules/
 * mod_demo_apollo510.ld. The 32 KB of executable MRAM that used to be carved
 * here (0x488000..0x490000) is region now, which is the whole point: the image
 * was being counted twice, once as a .rodata blob in the code window and again
 * as the slot it was programmed into. Every other ARM part still carves the
 * slot -- each needs its own RAM window and its own verification, and only
 * Apollo510 has been measured (TikuBench tests/memory/test_mem_tcm.c). */
/* Canonical NVM layout: inputs + shared derivation (arch/common). */
__tiku_layout_top          = 0x00800000;
__tiku_layout_code_cap     = ORIGIN(MRAM) + LENGTH(MRAM);  /* end of code */
__tiku_layout_module_size  = 0x0;                          /* none: ITCM  */
__tiku_layout_durable_size = 0x10000;                      /* 64 KB mirror */
INCLUDE arch/common/tiku_nvm_layout.ld

/* Carved NVM store/tier region (substrate B): ALL MRAM between the code window
 * and the 64 KB mirror -- ~3.7 MB on this part. Read in place by pointer
 * (PRIVDEFENA's default map covers MRAM above the code window); written via the
 * bootrom backend (kernel/memory/tiku_nvm_region). The NVM tier and file store
 * ride this. The base follows the code window directly now that no module slot
 * sits between them, so SHRINKING the window (P4) moves the FS -- which is a
 * reformat, not a silent corruption: the store's superblock carries its own
 * geometry and a mount over foreign bytes fails cleanly. */
/* (__tiku_nvmfs_base/__tiku_nvmfs_size come from the shared derivation.) */

/* NVM mirror page (mem port C, Proposal A): 64 KB at the top of MRAM
 * (0x7F0000..0x800000), where tiku_mem_arch_nvm_flush() programs the .uninit
 * snapshot. Grown from 32 KB for more durable cells/aliases headroom. The SBL
 * (0x400000) is never touched. */
__tiku_nvm_mram_start = __tiku_layout_durable_start;
__tiku_nvm_mram_size  = __tiku_layout_durable_size;
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
        /* DURABLE grade: exact-match .persistent -- this window is
         * MRAM-mirrored (and on apollo510 MPU-read-only outside unlock
         * windows).  Exact match so the WARM grade below cannot be
         * wildcard-swallowed into it.  (The old EXCLUDE_FILE net-kit
         * routing is retired: net buffers are placed explicitly at
         * their declarations, so no net object emits .persistent.) */
        *(.persistent)
        . = ALIGN(32);
        __uninit_end = .;
        /* NOTE: the WARM grade does NOT live here.  DTCM is wiped across
         * every reset on this part (hardware-verified 2026-07-15: a J-Link
         * marker in DTCM reads back all-zeros after a reset while an SSRAM
         * marker survives -- the secure bootloader runs through TCM before
         * our crt).  DURABLE (.persistent above) only survives because the
         * MRAM mirror restores it; WARM has no mirror, so it lives in the
         * retained SSRAM instead -- see .ssram below. */
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
        /* WARM grade (TIKU_PERSIST_WARM): survives warm resets, reseeded on
         * power cycle (magic self-primes).  Placed AFTER __ssram_end so the
         * crt's .ssram zero loop (__ssram_start..__ssram_end) skips it, and
         * in SSRAM rather than DTCM because DTCM is wiped across every reset
         * on this part (hardware-verified; see the .uninit note above). */
        *(.persistent.warm*)
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
           "ERROR: .uninit too large for the 64 KB NVM mirror page (mem port C)")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
