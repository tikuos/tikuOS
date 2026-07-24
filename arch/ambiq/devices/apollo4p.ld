/*
 * Tiku Operating System v0.05 - Ambiq Apollo4 Plus (Cortex-M4F) linker script
 *
 * Memory map (AmbiqSuite R4.5.0 apollo4p_evb / AMAP42KP):
 *   MRAM   : 2 MB   based at 0x0       (flash; low 96 KB reserved for boot/info)
 *   TCM    : 384 KB at 0x10000000      (.data / .bss / heap / stack)
 *   SSRAM  : 2 MB   at 0x10060000      (shared SRAM, both 1 MB banks, after TCM)
 *
 * This is the Apollo4 Lite script (apollo4l.ld) with the shared-SRAM window
 * grown from 1 MB to the full 2 MB the Plus exposes through the two 1 MB SSRAM
 * banks (PWRENSSRAM = 0x3, already powered in tiku_crt_early_apollo4l.c).  The
 * Plus advertises 2.75 MB of SRAM; the extra ~0.75 MB sits beyond the two banks
 * the register-compatible apollo4l.h power map can reach, so it is left for a
 * later port that vendors the Plus's own SSRAM bank/power definitions.
 *
 * MPU note: for the Plus, tiku_mpu_apollo4l.c maps a 4 MB RW+XN region at
 * 0x10000000 (gated on TIKU_DEVICE_APOLLO4P), which fully covers the 2 MB SSRAM
 * (ends 0x10260000).  So the real bound on .ssram is the SSRAM *physical* end,
 * not the MPU window: the 1.625 MB SRAM tier plus ~182 KB of other .ssram
 * statics (the 64 KB MRAM-mirror buffer + misc) fit in 2 MB with ~206 KB spare.
 * The ASSERT below enforces the physical bound at link time so a future tier
 * bump can't silently run past SSRAM and fault at runtime.
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
    /* Code window: a fixed ~508 KB budget for code/rodata (512 KB minus the
     * 4 KB loadable-module slot carved off its top). Everything between the
     * module slot and the 64 KB mirror becomes the carved NVM region, so the
     * NVM store/tier gets the bulk of MRAM. A region overflow here means code
     * outgrew its budget. */
    MRAM  (rx)  : ORIGIN = 0x00018000, LENGTH = 0x78000
    TCM   (rwx) : ORIGIN = 0x10000000, LENGTH = 384K      /* .data/.bss/heap/stack          */
    SSRAM (rwx) : ORIGIN = 0x10060000, LENGTH = 2M        /* shared SRAM: both 1 MB banks    */
}

__flash_start = ORIGIN(MRAM);
__flash_end   = ORIGIN(MRAM) + LENGTH(MRAM);

/* Loadable-native-module slot (Tier 3, tiku_basic_module.h): 4 KB of MRAM at
 * the top of the former code window (0x97000..0x98000). Carved from the CODE
 * budget, NOT the NVM region below -- the NVM region's interior (FS extent,
 * PERSIST/ckpt reserved tail, tier) is end-anchored on __tiku_nvmfs_size, so
 * shrinking THAT would shift every durable tenant and destroy on-device data.
 * Keep in sync with TIKU_MODULE_CARVE_ADDR and modules/mod_demo_apollo4l.ld. */
__tiku_module_slot_size = 0x8000;                          /* 32 KB */
__tiku_module_slot      = ORIGIN(MRAM) + LENGTH(MRAM);     /* 0x90000 */

/* Carved NVM store/tier region (substrate B): ALL MRAM between the module
 * slot and the 64 KB mirror -- ~1.34 MB on this part. Read in place by
 * pointer (MPU region 0 maps all MRAM RO); written via the bootrom backend
 * (kernel/memory/tiku_nvm_region). The NVM memory tier and the file store ride
 * this. Base is PINNED (not derived from the code window) so the module-slot
 * carve cannot move the FS/persist layout. */
__tiku_nvmfs_base  = 0x00098000;                   /* just above the module slot */

/* NVM mirror page (Proposal A, small hot state): 64 KB at the top of MRAM
 * (0x1F0000..0x200000), where tiku_mem_arch_nvm_flush() programs the .uninit
 * snapshot. Grown from 32 KB for more durable cells/aliases headroom. */
__tiku_nvm_mram_start = 0x001F0000;                             /* top - 64 KB */
__tiku_nvm_mram_size  = 0x10000;                                /* 64 KB */
__tiku_nvmfs_size     = __tiku_nvm_mram_start - __tiku_nvmfs_base;  /* ~1.34 MB */
__sram_start  = ORIGIN(TCM);
__sram_end    = ORIGIN(TCM) + LENGTH(TCM);

/* The Cortex-M loads the initial SP from vector entry 0. Place it at the very
 * top of TCM and let the stack grow downward toward .bss. */
__stack       = __sram_end;

SECTIONS
{
    /* -------- 1. Vector table (at user-MRAM origin 0x18000) ----------- */
    /* Apollo4 Plus has 84 external IRQs, so the table needs >=400 bytes ->
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
    /* ALIGN(8192) + the 8 KB pad below give the region a PMSAv7-legal
     * power-of-2 envelope: base 8K-aligned, size exactly 8 KB, so MPU
     * region 3 can hold it read-only outside NVM unlock windows (real
     * cell protection -- the shared M4F backend tiku_mpu_apollo4l.c
     * arms this on the Plus too).  Without the alignment the hardware
     * window covered base & ~0x1FFF instead: the last few hundred bytes
     * of .bss (newlib's __malloc_free_list/errno) plus the heap start
     * landed inside the read-only region, and the first un-windowed
     * libc/heap write -- the threads TLS test tripped it -- hardfaulted. */
    .uninit (NOLOAD) : ALIGN(8192)
    {
        __uninit_start = .;
        *(.uninit*)
        /* DURABLE grade: exact-match .persistent -- this window is
         * MRAM-mirrored and MPU-read-only outside unlock windows.
         * Exact match so the WARM grade below cannot be
         * wildcard-swallowed into it.  (The old EXCLUDE_FILE net-kit
         * routing is retired: net buffers are placed explicitly at
         * their declarations, so no net object emits .persistent.) */
        *(.persistent)
        . = ALIGN(32);
        __uninit_end = .;
        /* WARM grade (TIKU_PERSIST_WARM): survives warm resets, but
         * sits OUTSIDE the mirror and the MPU-RO window.  Everything
         * here chose the grade on purpose at its declaration. */
        /* Pad to the full 8 KB MPU envelope FIRST: the WARM grade
         * below must sit outside the read-only region. */
        . = __uninit_start + 8192;
        *(.persistent.warm*)
    } > TCM

    ASSERT((__uninit_end - __uninit_start) <= 8192,
           "ERROR: .uninit exceeds the 8 KB PMSAv7 MPU envelope (grow the region size in apollo4p.ld AND tiku_mpu_apollo4l.c together)")

    _end = .;
    end = .;
    __end__ = .;
    __datastart = __data_start;

    /* Shared SRAM (2 MB at 0x10060000). Holds the large volatile tier backing
     * buffers (.ssram) -- 1.5 MB SRAM tier + the 64 KB MRAM-mirror staging
     * buffer -- zero-initialised by the reset handler. ALIGN(32) for a PMSAv7
     * MPU region base. */
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

    /* The module slot must sit exactly between the code window and the NVM
     * region -- a hole in neither, an overlap with neither. */
    ASSERT(__tiku_module_slot + __tiku_module_slot_size == __tiku_nvmfs_base,
           "ERROR: module slot does not abut __tiku_nvmfs_base (layout drift)")


    /* .ssram must stay inside the 2 MB physical SSRAM (0x10060000..0x10260000);
     * the Plus's MPU region 1 is 4 MB so it already covers this.  If the SRAM
     * tier ever outgrows physical SSRAM, the tail would fault at runtime. */
    ASSERT(__ssram_end <= 0x10260000,
           "ERROR: .ssram (SRAM tier) runs past the 2 MB physical SSRAM -- shrink TIKU_TIER_SRAM_SIZE")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
