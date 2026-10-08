/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * apollo4p.ld - Ambiq Apollo4 Plus (Cortex-M4F) linker script.
 *
 * The image starts at user MRAM 0x00018000 with the vector table first, and
 * the shared SRAM is the Plus's two 1 MB SSRAM banks.  Memory map and
 * exported symbols below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Memory map (AmbiqSuite R4.5.0 apollo4p_evb / AMAP42KP):
 *   MRAM   : 2 MB   at 0x0          (low 96 KB reserved for boot code)
 *   TCM    : 384 KB at 0x10000000   (.data, .bss, .uninit, stack)
 *   SSRAM  : 2 MB   at 0x10060000   (shared SRAM: both 1 MB banks)
 *
 * tiku_crt_early_apollo4l.c powers both SSRAM banks (PWRENSSRAM = 0x3).  The
 * Plus has 2.75 MB of SRAM in all; the rest lies outside the two banks that
 * the apollo4l.h power map reaches, and this script does not use it.
 *
 * On the Plus, tiku_mpu_apollo4l.c maps a 4 MB RW+XN region at 0x10000000,
 * which covers all of SSRAM (it ends at 0x10260000), so the bound on .ssram
 * is the physical end of SSRAM; an ASSERT at the end checks it.
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
    MRAM  (rx)  : ORIGIN = 0x00018000, LENGTH = 0x60000   /* code window */
    TCM   (rwx) : ORIGIN = 0x10000000, LENGTH = 384K      /* TCM */
    SSRAM (rwx) : ORIGIN = 0x10060000, LENGTH = 2M        /* both banks */
}

__flash_start = ORIGIN(MRAM);
__flash_end   = ORIGIN(MRAM) + LENGTH(MRAM);

/* Module slot: when the BASIC module loader is built, the Makefile's
 * __tiku_module_reserve takes the top 32 KB of the code window
 * (0x70000..0x78000), so the slot comes out of the code budget and the NVM
 * region keeps its size.  The region's tenants are placed from its end, so
 * a change to its size moves every durable tenant and loses the data on the
 * device.  0x70000 must equal TIKU_MODULE_CARVE_ADDR and the CARVE origin in
 * basic/modules/mod_demo_apollo4l.ld. */
/* NVM layout inputs; arch/common/tiku_nvm_layout.ld derives the rest. */
__tiku_layout_top          = 0x00200000;
__tiku_layout_code_cap     = ORIGIN(MRAM) + LENGTH(MRAM);  /* end of code */
__tiku_layout_persist_size = 0x4000;                       /* 16 KB mirror */
INCLUDE arch/common/tiku_nvm_layout.ld

/* Carved NVM region (__tiku_nvmfs_base/__tiku_nvmfs_size, from the shared
 * derivation): all MRAM from the end of the code window (0x78000) to the
 * persist mirror, holding the NVM tier and /data.  Code reads it in place
 * (MPU region 0 maps all MRAM read-only) and writes it through the boot ROM
 * (kernel/memory/tiku_nvm_region).  The module reserve does not move it. */

/* Persist mirror: the top 16 KB of MRAM (0x1FC000..0x200000), where
 * tiku_mem_arch_nvm_flush() programs the .uninit image. */
__tiku_nvm_mram_start = __tiku_layout_persist_start;
__tiku_nvm_mram_size  = __tiku_layout_persist_size;
__sram_start  = ORIGIN(TCM);
__sram_end    = ORIGIN(TCM) + LENGTH(TCM);

/* The initial SP (vector entry 0) is the top of TCM.  The stack grows down
 * through a 32 KB budget, with a 4 KB MPU guard below it. */
__stack       = __sram_end;
__tiku_stack_bottom = __stack - 32K;
__tiku_stack_guard_start = __tiku_stack_bottom - 4K;
__ssram_bank_start = ORIGIN(SSRAM);
__ssram_bank_end = ORIGIN(SSRAM) + LENGTH(SSRAM);

SECTIONS
{
    /* -------- 1. Vector table (at user-MRAM origin 0x18000) ----------- */
    /* 16 + 84 entries take 400 bytes, so VTOR needs 512-byte alignment;
     * 0x18000 satisfies it.  The reset handler also writes VTOR. */
    .vectors ORIGIN(MRAM) : ALIGN(512)
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        __vectors_end = .;
    } > MRAM

    /* __Vectors is the CMSIS name for the vector table, which this port
     * defines as tiku_ambiq_vectors. */
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
        /* Persist-cell table: boot carries each cell by its key when an
         * update moves it (tiku_persist_move_boot, tiku_mem.h). */
        . = ALIGN(4);
        __tiku_cells_start = .;
        KEEP(*(.tiku_cells))
        __tiku_cells_end = .;
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
    /* ALIGN(8192) and the pad to 8 KB below make .uninit a legal PMSAv7
     * region: an 8 KB-aligned base and a size of exactly 8 KB.  MPU region 3
     * (tiku_mpu_apollo4l.c) holds it read-only outside NVM unlock windows.
     * PMSAv7 rounds a region base down to a multiple of the region size,
     * so an unaligned .uninit puts the read-only region over the end of
     * .bss, where a libc write faults. */
    .uninit (NOLOAD) : ALIGN(8192)
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
        /* RETAINED grade: survives a warm reset, and sits outside the
         * mirror.  The pad to the end of the 8 KB envelope comes first, so
         * .retained starts past the read-only MPU region. */
        . = __uninit_start + 8192;
        *(.retained*)
    } > TCM

    ASSERT((__uninit_end - __uninit_start) <= 8192,
           "ERROR: .uninit exceeds the 8 KB PMSAv7 MPU envelope (grow the region size in apollo4p.ld AND tiku_mpu_apollo4l.c together)")

    _end = .;
    end = .;
    __end__ = .;
    __datastart = __data_start;

    /* The extra SRAM tier span runs from past the 8 KB envelope and the
     * RETAINED data up to the stack guard. */
    __tier_sram_extra_start = ALIGN(_end, 32);
    __tier_sram_extra_end = __tiku_stack_guard_start;
    ASSERT(__tier_sram_extra_start <= __tier_sram_extra_end,
           "ERROR: TCM statics overlap the stack guard")
    ASSERT((__tiku_stack_guard_start & 0xFFF) == 0,
           "ERROR: Apollo4 stack guard must be 4K aligned")

    /* Large static buffers (.ssram) in the 2 MB shared SRAM, zeroed by the
     * reset handler.  ALIGN(32) for a PMSAv7 MPU region base. */
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
        . = ALIGN(32);
        __ssram_top = .;
    } > SSRAM

    /*
     * The primary SRAM tier span is the rest of the shared bank: from
     * __ssram_top, past every static tenant, to the end of the bank.  It lies
     * outside the crt's .ssram zero loop; the tier allocator does not promise
     * zeroed memory.  No tail reserve: the stack and its guard are in TCM.
     */
    __tiku_tier_span_start      = __ssram_top;
    __tiku_tier_span_limit      = ORIGIN(SSRAM) + LENGTH(SSRAM);
    __tiku_tier_reserve_default = 0;
    __tiku_tier_floor_default   = 0x40000;
    INCLUDE arch/common/tiku_sram_layout.ld

    ASSERT(_end <= __tiku_stack_guard_start,
           "ERROR: TCM exhausted -- need 32K stack plus 4K guard")

    /* The MRAM image (code, rodata and the .data load copy) must end below
     * __tiku_code_limit: the end of the code window, less the module slot in
     * builds with the loader. */
    ASSERT(__data_load + (__data_end - __data_start) <= __tiku_code_limit,
           "ERROR: MRAM image overruns the code window (or its module reserve)")

    /* The mirror holds the 16-byte V2 header and the whole .uninit image. */
    ASSERT((__uninit_end - __uninit_start) <= (__tiku_nvm_mram_size - 16),
           "ERROR: .uninit too large for the 16 KB NVM mirror page")

    /* tiku_layout_pin must start .uninit, so a firmware image whose other
     * durable variables differ still finds the layout record at the offset
     * the previous image wrote it to. */
    ASSERT((DEFINED(tiku_layout_pin) ? tiku_layout_pin : __uninit_start)
           == __uninit_start,
           "ERROR: tiku_layout_pin does not open .uninit (TIKU_DURABLE_FIRST holds one object)")


    /* .ssram must end inside the 2 MB of physical SSRAM (ending 0x10260000).
     * MPU region 1 spans 4 MB, so it does not catch an overrun. */
    ASSERT(__ssram_end <= 0x10260000,
           "ERROR: .ssram (SRAM tier) runs past the 2 MB physical SSRAM -- shrink the .ssram statics")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
