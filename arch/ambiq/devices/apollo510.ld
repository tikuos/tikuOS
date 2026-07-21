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
    /* Code window: a fixed ~508 KB budget for code/rodata (512 KB minus the
     * 4 KB loadable-module slot carved off its top). Everything between the
     * module slot and the 64 KB mirror becomes the carved NVM region, so the
     * NVM store/tier gets the bulk of MRAM (~3.4 MB). A region overflow here
     * means code outgrew its budget. */
    MRAM  (rx)  : ORIGIN = 0x00410000, LENGTH = 0x7F000
    TCM   (rwx) : ORIGIN = 0x20000000, LENGTH = 512K      /* DTCM: .data/.bss/heap/stack    */
    SSRAM (rwx) : ORIGIN = 0x20080000, LENGTH = 3M        /* shared SRAM (unused at bring-up) */
}

__flash_start = ORIGIN(MRAM);
__flash_end   = ORIGIN(MRAM) + LENGTH(MRAM);

/* Loadable-native-module slot (Tier 3, tiku_basic_module.h): 4 KB of MRAM at
 * the top of the former code window (0x48F000..0x490000). A module is linked
 * at this VMA; the loader programs its image here via the bootrom (gate-last)
 * and runs it XIP. Carved from the CODE budget, NOT the NVM region below --
 * the NVM region's interior (FS extent, PERSIST/ckpt reserved tail, tier) is
 * end-anchored on __tiku_nvmfs_size, so shrinking THAT would shift every
 * durable tenant and destroy on-device data. Keep in sync with
 * TIKU_MODULE_CARVE_ADDR and modules/mod_demo_apollo510.ld. */
__tiku_module_slot_size = 0x1000;                          /* 4 KB */
__tiku_module_slot      = ORIGIN(MRAM) + LENGTH(MRAM);     /* 0x48F000 */

/* Carved NVM store/tier region (substrate B): ALL MRAM between the module
 * slot and the 64 KB mirror -- ~3.4 MB on this part. Read in place by pointer
 * (PRIVDEFENA's default map covers MRAM above the code window); written via the
 * bootrom backend (kernel/memory/tiku_nvm_region). The NVM tier and file store
 * ride this. Base is PINNED (not derived from the code window) so the module-
 * slot carve cannot move the FS/persist layout. */
__tiku_nvmfs_base = 0x00490000;                    /* just above the module slot */

/* NVM mirror page (mem port C, Proposal A): 64 KB at the top of MRAM
 * (0x7F0000..0x800000), where tiku_mem_arch_nvm_flush() programs the .uninit
 * snapshot. Grown from 32 KB for more durable cells/aliases headroom. The SBL
 * (0x400000) is never touched. */
__tiku_nvm_mram_start = 0x007F0000;                            /* top - 64 KB */
__tiku_nvm_mram_size  = 0x10000;                               /* 64 KB */
__tiku_nvmfs_size     = __tiku_nvm_mram_start - __tiku_nvmfs_base;  /* ~3.4 MB */
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

    /* The module slot must sit exactly between the code window and the NVM
     * region -- a hole in neither, an overlap with neither. */
    ASSERT(__tiku_module_slot + __tiku_module_slot_size == __tiku_nvmfs_base,
           "ERROR: module slot does not abut __tiku_nvmfs_base (layout drift)")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
