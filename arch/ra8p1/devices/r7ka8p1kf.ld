/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * r7ka8p1kf.ld - R7KA8P1KF (EK-RA8P1) linker script, MRAM-resident image.
 *
 * The image runs from MRAM, so a reset re-enters it.  `.persistent` sits in
 * a reserved MRAM carve and is written in place: this MRAM is byte-writable
 * without an erase, so durable data needs no SRAM mirror.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

ENTRY(tiku_ra8p1_reset_handler)

MEMORY
{
    MRAM (rx)  : ORIGIN = 0x02000000, LENGTH = 1024K
    /* The first 16 KB of SRAM hold the CPU1 payload, which is linked at
     * 0x22000000 and uses absolute addresses.  Keep in step with
     * TIKU_CPU1_AREA_ADDR (tiku_cpu1_ipc.h) and BASE in
     * arch/ra8p1/cpu1/tiku_cpu1.ld. */
    SRAM (rwx) : ORIGIN = 0x22004000, LENGTH = 1664K - 16K
}

/* Memory extents: __flash_* is the code MRAM.  The kernel's memory report
 * reads __sram_start and __sram_end. */
__flash_start = ORIGIN(MRAM);
__flash_end   = ORIGIN(MRAM) + LENGTH(MRAM);
__sram_start  = ORIGIN(SRAM);
__sram_end    = ORIGIN(SRAM) + LENGTH(SRAM);

/*
 * NVM layout: the inputs below, derived by arch/common/tiku_nvm_layout.ld.
 *
 *   0x02000000  code               384 KB
 *   0x02060000  NVM region         624 KB (NVM tier, 32 KB by default; /data)
 *   0x020FC000  persist partition   16 KB (`.persistent`, written in place)
 *   0x02100000  top
 *
 * Changing a size moves the file store or the persist cells; moved cells
 * re-prime on an established board (/sys/persist reports primed != 0).
 */
__tiku_layout_top          = ORIGIN(MRAM) + LENGTH(MRAM);
__tiku_layout_code_cap     = ORIGIN(MRAM) + 0x60000;        /* 384 KB code */
__tiku_layout_persist_size = 0x4000;                        /* 16 KB persist */
INCLUDE arch/common/tiku_nvm_layout.ld

/* Platform aliases: __tiku_nvm_mram_* is the persist partition, where
 * `.persistent` lives and is written in place. */
__tiku_nvm_mram_size  = __tiku_layout_persist_size;
__tiku_nvm_mram_start = __tiku_layout_persist_start;
__tiku_nvm_mram_end   = __tiku_layout_top;

/* The Cortex-M loads the initial SP from vector[0]: the top of SRAM.  The
 * stack grows down from there, inside the MPU's stack reserve
 * (tiku_mpu_arch.c). */
__stack = __sram_end;

SECTIONS
{
    /* -------- Vectors (the MRAM base, the reset address) ------------- */
    .vectors ORIGIN(MRAM) : ALIGN(512)
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        __vectors_end = .;
    } > MRAM

    /*
     * Every MPU boundary is 32-byte aligned.  PMSAv8's region granule is 32
     * bytes (RBAR holds base[31:5], RLAR limit[31:5]); a boundary that is not
     * 32-aligned would make a region overlap its neighbour or leave a hole
     * that falls through to the background map.
     */
    .text : ALIGN(32)
    {
        *(.text*)
        *(.glue_7) *(.glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
        *(.rodata*)
        *(.gnu.linkonce.r.*)
        /* Persist-cell table: boot carries each cell by its key when an
         * update moves it (tiku_persist_move_boot, tiku_mem.h). */
        . = ALIGN(4);
        __tiku_cells_start = .;
        KEEP(*(.tiku_cells))
        __tiku_cells_end = .;
        . = ALIGN(32);
    } > MRAM

    /* ARM unwind tables.  .ARM.exidx needs this explicit output section: its
     * default placement collides with .data's load address and fails the
     * link. */
    .ARM.extab : { *(.ARM.extab*) } > MRAM
    .ARM.exidx : ALIGN(32)
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
        . = ALIGN(32);
    } > MRAM

    _etext = .;
    etext  = .;

    /*
     * .data runs from SRAM and loads from MRAM; __data_load is
     * LOADADDR(.data), set after the section.  A `.` captured before it can
     * differ from the load address by .data's 32-byte alignment padding, and
     * the startup copy would then shift every initialised variable.
     */
    .data : ALIGN(32)
    {
        __data_start = .;
        __datastart = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM AT > MRAM

    __data_load = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(32)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SRAM

    /* -------- Warm-reset survivor state (SRAM) ----------------------- */
    /* `.uninit` and TIKU_RETAINED data sit outside .bss, so the reset
     * handler does not zero them: they survive a warm reset and hold no valid
     * data after a power cycle.  Durable data is `.persistent`, in MRAM.
     *
     * TIKU_RETAINED data is not MPU-protected, and its writers open no NVM
     * window: tiku_hang_boot_init() clears the hang record at every boot.
     * Placed in the protected .persistent window, that write would
     * MemManage-fault at boot. */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        *(.uninit*)
        . = ALIGN(32);
        *(.retained*)
        . = ALIGN(32);
        __uninit_end = .;
    } > SRAM

    _end = .;
    end = .;
    __end__ = .;
    PROVIDE(__heap_start = .);

    /*
     * SRAM tier: the SRAM between the statics and the MPU's stack reserve,
     * less a 32 KB gap (__tiku_tier_reserve_default).  The span is derived at
     * link time, so it follows .bss in every build configuration.  It lies
     * outside .bss, and the startup code does not zero it.
     *
     * A stack frame that strides past the 4 KB stack guard lands in the gap
     * before it reaches the tier.
     */
    /* The span limit is the stack top less the MPU stack budget (0x8000,
     * MPU_STACK_RESERVED_BYTES in tiku_mpu_arch.c); the fragment applies the
     * reserve, resolves the floor from the make line and asserts both. */
    __tiku_tier_span_start      = ALIGN(32);
    __tiku_tier_span_limit      = __stack - 0x8000;
    __tiku_tier_reserve_default = 32K;
    __tiku_tier_floor_default   = 0x40000;
    INCLUDE arch/common/tiku_sram_layout.ld

    /* The statics must end at least 36 KB (0x9000) below the stack top.  The
     * MPU's stack reserve is the top 32 KB (MPU_STACK_RESERVED_BYTES in
     * tiku_mpu_arch.c, its 4 KB guard included). The extra 4 KB is an
     * allocator margin below that reserve, not a second guard. */
    ASSERT((__stack - _end) >= 0x9000,
           "ERROR: RA8P1 SRAM exhausted: need 32K stack reserve + 4K allocator margin")

    /* -------- Persist partition (MRAM, byte-writable in place) ------- */
    /* NOLOAD: not in the programmed image, so a reflash leaves it alone, and
     * on a new part the persist-cell gates find no magic and prime their
     * defaults.
     *
     * Every store into `.persistent` must sit between tiku_mpu_unlock_nvm()
     * and tiku_mpu_lock_nvm().  Outside that window the MPU region is
     * read-only and MRAM programming is disabled (MRCPC1.MRCPSEN). */
    .persistent __tiku_nvm_mram_start (NOLOAD) : ALIGN(32)
    {
        __persistent_start = .;
        /* TIKU_DURABLE_FIRST: the layout record opens the partition, at the
         * same offset in every image; the rest follow link order. */
        KEEP(*(.persistent.layout))
        *(.persistent)
        . = ALIGN(32);
        __persistent_end = .;
    } > MRAM

    /* The MRAM image (code, rodata and the .data load copy) must end below
     * __tiku_code_limit: the NVM region, or a module slot reserved at the top
     * of the code window (__tiku_module_reserve). */
    ASSERT(__data_load + (__data_end - __data_start) <= __tiku_code_limit,
           "ERROR: RA8P1 MRAM image overruns the code window (or its module reserve)")

    /* The persist-partition data must fit inside the reserved span. */
    ASSERT(__persistent_end <= __tiku_nvm_mram_end,
           "ERROR: .persistent overruns the persist partition")

    /* The layout record opens .persistent in every build, so images with
     * different durable variables find it at the same address. */
    ASSERT((DEFINED(tiku_layout_pin) ? tiku_layout_pin : __persistent_start)
           == __persistent_start,
           "ERROR: tiku_layout_pin does not open .persistent (TIKU_DURABLE_FIRST holds one object)")

    PROVIDE(__tiku_cpu1_area = 0x22000000);

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
