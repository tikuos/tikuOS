/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * apollo510.ld - Ambiq Apollo510 (Cortex-M55) linker script.
 *
 * The image starts at user MRAM 0x00410000 with the vector table first; the
 * Secure Boot Loader loads the initial SP and reset PC from it.  Memory map
 * and exported symbols below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Memory map (AmbiqSuite am_reg_base_addresses.h):
 *   ITCM   : 256 KB at 0x00000000   (no section; modules run at 0x1000)
 *   MRAM   : 4 MB   at 0x00400000   (low 64 KB holds the SBL)
 *   DTCM   : 512 KB at 0x20000000   (.data, .bss, .uninit, stack)
 *   SSRAM  : 3 MB   at 0x20080000   (.ssram, .retained, SRAM tier)
 *
 * The image needs no boot header: the Secure Boot Loader in MRAM
 * 0x400000..0x410000 starts it from its vector table at 0x410000.
 *
 * Symbols the shared startup, memory and region code read:
 *   __stack, __sram_start/end, __flash_start/end,
 *   __data_load/start/end, __bss_start/end, __uninit_start/end,
 *   _end / end / __end__ / __datastart
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(tiku_ambiq_reset_handler)

MEMORY
{
    /* Code window: 384 KB for code and rodata.  Everything from its end to
     * the 16 KB persist mirror is the carved NVM region, which the NVM tier
     * and /data share, so changing LENGTH moves the region and /data with
     * it. */
    MRAM  (rx)  : ORIGIN = 0x00410000, LENGTH = 0x60000   /* code window */
    TCM   (rwx) : ORIGIN = 0x20000000, LENGTH = 512K      /* DTCM */
    SSRAM (rwx) : ORIGIN = 0x20080000, LENGTH = 3M        /* shared SRAM */
}

__flash_start = ORIGIN(MRAM);
__flash_end   = ORIGIN(MRAM) + LENGTH(MRAM);

/* No module slot: the Makefile passes no __tiku_module_reserve for this
 * part.  The BASIC module loader copies a module image from /data into an
 * ITCM window and runs it there (tiku_basic_module.h,
 * kernel/shell/basic/modules/mod_demo_apollo510.ld). */
/* NVM layout inputs; arch/common/tiku_nvm_layout.ld derives the rest. */
__tiku_layout_top          = 0x00800000;
__tiku_layout_code_cap     = ORIGIN(MRAM) + LENGTH(MRAM);  /* end of code */
__tiku_layout_persist_size = 0x4000;                       /* 16 KB mirror */
INCLUDE arch/common/tiku_nvm_layout.ld

/* Carved NVM region (__tiku_nvmfs_base/__tiku_nvmfs_size, from the shared
 * derivation): all MRAM from the end of the code window (0x470000) to the
 * persist mirror, holding the NVM tier and /data.  Code reads it in place
 * through the MPU's default map (PRIVDEFENA) and writes it through the boot
 * ROM (kernel/memory/tiku_nvm_region).  The store records its own geometry,
 * so after the region moves the old store is not mounted. */

/* Persist mirror: the top 16 KB of MRAM (0x7FC000..0x800000), where
 * tiku_mem_arch_nvm_flush() programs the .uninit image.  Nothing here places
 * data in the SBL's 0x400000..0x410000. */
__tiku_nvm_mram_start = __tiku_layout_persist_start;
__tiku_nvm_mram_size  = __tiku_layout_persist_size;
__sram_start  = ORIGIN(TCM);
__sram_end    = ORIGIN(TCM) + LENGTH(TCM);

/* The initial SP (vector entry 0) is the top of DTCM.  The stack grows down
 * through a 32 KB budget, with a 4 KB MPU guard below it. */
__stack       = __sram_end;
__tiku_stack_bottom = __stack - 32K;
__tiku_stack_guard_start = __tiku_stack_bottom - 4K;
__ssram_bank_start = ORIGIN(SSRAM);
__ssram_bank_end = ORIGIN(SSRAM) + LENGTH(SSRAM);

SECTIONS
{
    /* -------- 1. Vector table (at user-MRAM origin 0x410000) ---------- */
    /* 16 + 135 entries take 604 bytes; VTOR needs 1024-byte alignment. */
    .vectors ORIGIN(MRAM) : ALIGN(1024)
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        __vectors_end = .;
    } > MRAM

    ASSERT((__vectors_start & 1023) == 0, "VTOR requires 1024-byte alignment")

    /* __Vectors is the CMSIS name for the vector table, which this port
     * defines as tiku_ambiq_vectors. */
    __Vectors = tiku_ambiq_vectors;

    /* -------- 2. Code & rodata --------------------------------------- */
    .text : ALIGN(4)
    {
        *(.text*)
        *(.glue_7) *(.glue_7t)
        /* Input marked .itcm_text links here and runs in place from MRAM. */
        *(.itcm_text*)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > MRAM

    /* End of .text: `free` and /sys/mem/free size the MRAM-resident code
     * from _etext.  `etext` is the same address under the older name. */
    _etext = .;
    etext  = .;

    .rodata : ALIGN(4)
    {
        *(.rodata*)
        *(.gnu.linkonce.r.*)
        /* Persist-cell table: boot carries each cell by its key when an
         * update moves it (tiku_persist_move_boot, tiku_mem.h). */
        . = ALIGN(4);
        __tiku_cells_start = .;
        KEEP(*(.tiku_cells))
        __tiku_cells_end = .;
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
    /* The MemManage/HardFault handler records the violation here.  It sits
     * after .bss, so the crt zero loop (which stops at __bss_end) leaves it
     * alone, and before .uninit, so it lies in the writable SRAM_LO MPU
     * region and is not mirrored to MRAM. */
    .mpu_diag (NOLOAD) : ALIGN(32)
    {
        __mpu_diag_start = .;
        *(.mpu_diag*)
        . = ALIGN(32);
        __mpu_diag_end = .;
    } > TCM

    /* -------- 5. Persistent (skipped by zero-init) ------------------- */
    /* The crt does not zero .uninit; tiku_mem_arch_init() restores it from
     * the MRAM mirror at boot, or zeroes it when the mirror holds no image.
     * ALIGN(32) for an ARMv8-M MPU region base. */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        /* TIKU_DURABLE_FIRST: the layout record, when built in, is first in
         * the mirrored image, at offset 0; the other durable data follows in
         * link order. */
        KEEP(*(.persistent.layout))
        *(.uninit*)
        /* DURABLE grade (.persistent): the window the MRAM mirror holds. */
        *(.persistent)
        . = ALIGN(32);
        __uninit_end = .;
        /* The RETAINED grade is in SSRAM (see .ssram below), not here: the
         * secure bootloader clears part of DTCM on every reset, so DTCM data
         * survives a reset only through the MRAM mirror. */
    } > TCM

    /* End of static allocations / start of the _sbrk heap. Aliases for
     * whichever name the linked code uses (tikuOS, newlib, libgloss). */
    _end = .;
    end = .;
    __end__ = .;
    __datastart = __data_start;

    /* The extra SRAM tier span runs from the end of the statics up to the
     * stack guard. */
    __tier_sram_extra_start = ALIGN(_end, 32);
    __tier_sram_extra_end = __tiku_stack_guard_start;
    ASSERT(__tier_sram_extra_start <= __tier_sram_extra_end,
           "ERROR: DTCM statics overlap the stack guard")

    /* Large static buffers (.ssram) in the 3 MB shared SRAM, which
     * tiku_crt_early.c powers and then zeroes.  ALIGN(32) for an MPU region
     * base. */
    .ssram (NOLOAD) : ALIGN(32)
    {
        __ssram_start = .;
        *(.ssram*)
        . = ALIGN(32);
        __ssram_end = .;
        /* RETAINED grade: survives a warm reset and is lost on a power
         * cycle.  It sits past __ssram_end, outside the crt's .ssram zero
         * loop. */
        *(.retained*)
    } > SSRAM

    /* Collects any .shared and .resource_table input; __ssram_top marks the
     * end of the bank's static tenants. */
    .shared (NOLOAD) :
    {
        KEEP(*(.resource_table))
        *(.shared*)
        . = ALIGN(32);
        __ssram_top = .;
    } > SSRAM

    /*
     * The primary SRAM tier span is the rest of the shared bank: from
     * __ssram_top, past every static tenant including the RETAINED data, to
     * the end of the bank.  It lies outside the crt's .ssram zero loop; the
     * tier allocator does not promise zeroed memory.  No tail reserve: the
     * stack and its guard are in DTCM.
     */
    __tiku_tier_span_start      = __ssram_top;
    __tiku_tier_span_limit      = ORIGIN(SSRAM) + LENGTH(SSRAM);
    __tiku_tier_reserve_default = 0;
    __tiku_tier_floor_default   = 0x50000;
    INCLUDE arch/common/tiku_sram_layout.ld

    ASSERT(_end <= __tiku_stack_guard_start,
           "ERROR: DTCM exhausted — need 32K stack plus 4K guard")

    /* The mirror holds the 16-byte V2 header and the whole .uninit image. */
    ASSERT((__uninit_end - __uninit_start) <= (__tiku_nvm_mram_size - 16),
           "ERROR: .uninit too large for the 16 KB NVM mirror page (mem port C)")

    /* tiku_layout_pin must start .uninit, so a firmware image whose other
     * durable variables differ still finds the layout record at the offset
     * the previous image wrote it to. */
    ASSERT((DEFINED(tiku_layout_pin) ? tiku_layout_pin : __uninit_start)
           == __uninit_start,
           "ERROR: tiku_layout_pin does not open .uninit (TIKU_DURABLE_FIRST holds one object)")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
