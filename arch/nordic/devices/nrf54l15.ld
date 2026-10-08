/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * nrf54l15.ld - Nordic nRF54L15 (nRF54L15-DK) linker script.
 *
 * Vectors at the RRAM base (VTOR=0), then code and the .data image, with the
 * NVM region and the persist partition above the code window, both
 * byte-writable behind the RRAMC WEN gate.  Map and exported symbols below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Memory map (nRF54L15 datasheet):
 *   RRAM  : 1.5 MB non-volatile, write-in-place, at 0x00000000
 *   SRAM  : 256 KB at 0x20000000
 *
 * Layout in RRAM (grows up from 0x0):
 *   0x00000000 .vectors     (Cortex-M33 vector table; the CPU loads SP from
 *                            word 0 and the reset PC from word 1, so the
 *                            table sits at the RRAM base, which meets the
 *                            VTOR alignment)
 *   0x00000xxx .text/.rodata (code + read-only data)
 *   0x000xxxxx .data (LMA)   (initialised-data image, copied to SRAM at boot)
 *   ...        <free RRAM>    (code budget grows toward the reserved NVM tail)
 *   above code reserved      (two byte-writable NVM spans behind the RRAMC
 *                            WEN gate: the carved NVM region -- 32 KB tier +
 *                            TFS /data -- and, at the very top, the 16 KB
 *                            persist partition holding `.persistent`;
 *                            symbols below)
 *
 * Layout in SRAM (stack grows down from the top):
 *   .data              (initialised RAM, copied from its RRAM LMA at boot)
 *   .bss               (zeroed at boot)
 *   .uninit            (skipped by zero-init: warm-reset survivor state)
 *   .retained          (skipped by zero-init: warm-reset survivor state)
 *   __stack            (top of SRAM, grows downward)
 *
 * Symbols exposed to the kernel carry the other ARM ports' names:
 *   __stack __data_load __data_start __data_end __bss_start __bss_end
 *   __uninit_start __uninit_end _end end __end__ __datastart
 *   __sram_start __sram_end __flash_start __flash_end   (__flash_* = RRAM)
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(tiku_nordic_reset_handler)

MEMORY
{
    /* Usable application RRAM is 0x17D000 (1524 KB) of the 1536 KB array:
     * the top 12 KB is reserved (MDK NRF_MEMORY_FLASH_SIZE, and nrfx's own
     * linker script), and an access past 0x17CFFF bus-faults. */
    RRAM (rx)  : ORIGIN = 0x00000000, LENGTH = 0x17D000
    /* App SRAM is 240 KB of the 256 KB part: the top 16 KB
     * (0x2003C000..0x2003FFFF) is the FLPR coprocessor carve -- RISC-V
     * code, stack and the shared IPC page.  The length must match
     * TIKU_FLPR_RAM_* in arch/nordic/flpr/tiku_flpr_ipc.h.  Every build
     * reserves the carve, so the image layout does not change with
     * TIKU_FLPR_ENABLE. */
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x3C000
}

/* Region bounds: __flash_* is the RRAM, __sram_* the app SRAM. */
__flash_start = ORIGIN(RRAM);
__flash_end   = ORIGIN(RRAM) + LENGTH(RRAM);
__sram_start  = ORIGIN(SRAM);
__sram_end    = ORIGIN(SRAM) + LENGTH(SRAM);

/* Persist partition: the top 16 KB of RRAM, byte-writable in place behind
 * the RRAMC WEN gate, holding `.persistent` (persist cells, aliases, lc,
 * inittab).  Resizing either carve moves persist-cell and /data addresses: a
 * board flashed with the previous layout re-primes its cells (primed != 0)
 * and does not find its /data where it was. */
/* Canonical NVM layout: inputs + shared derivation (arch/common). */
__tiku_layout_top          = ORIGIN(RRAM) + LENGTH(RRAM);
__tiku_layout_code_cap     = 0x60000;                       /* 384 KB code */
/* The nRF54LM20A has the same 384 KB code window, so the module slot is at
 * one address on both parts. */
__tiku_layout_persist_size = 0x4000;                      /* 16 KB partition */
INCLUDE arch/common/tiku_nvm_layout.ld

__tiku_nvm_rram_size  = __tiku_layout_persist_size;
__tiku_nvm_rram_start = __tiku_layout_persist_start;
__tiku_nvm_rram_end   = __tiku_layout_top;

/* Carved NVM region (the tiku_nvm_region backend): the NVM tier (32 KB) at
 * the front, then the TFS extent (TIKU_NVMFS_FS_BYTES), which holds BASIC's
 * saved program and checkpoint as files.  It runs from the end of the code
 * window up to the persist partition and is written in place behind the WEN
 * gate, with no erase.  __tiku_nvmfs_size is an absolute symbol whose
 * address is the size. */
/* Loadable native module slot (basic/tiku_basic_module.h): the
 * top 32 KB of the code window, byte-writable and executable RRAM, reserved
 * only in builds with the loader (--defsym=__tiku_module_reserve).  The order
 * is code | module | NVM region | persist; TIKU_MODULE_CARVE_ADDR (0x58000)
 * must equal the code cap less 32 KB. */
/* (module slot + NVM region symbols come from the shared derivation.) */

/* The Cortex-M loads the initial SP from vector[0]; place it at the top of
 * SRAM and let the stack grow down toward .bss. */
__stack = __sram_end;
__tiku_stack_bottom = __stack - 32K;
__tiku_stack_guard_start = __tiku_stack_bottom - 4K;

SECTIONS
{
    /* -------- 1. Vectors (must be at RRAM base 0x0) ------------------- */
    .vectors ORIGIN(RRAM) :
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        __vectors_end = .;
    } > RRAM

    /* -------- 2. Code & rodata --------------------------------------- */
    .text : ALIGN(4)
    {
        *(.text*)
        *(.glue_7) *(.glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > RRAM

    _etext = .;
    etext  = .;

    .rodata : ALIGN(4)
    {
        *(.rodata*)
        /* Persist-cell table: boot carries each cell by its key when an
         * update moves it (tiku_persist_move_boot, tiku_mem.h). */
        . = ALIGN(4);
        __tiku_cells_start = .;
        KEEP(*(.tiku_cells))
        __tiku_cells_end = .;
        *(.gnu.linkonce.r.*)
    } > RRAM

    .ARM.extab : { *(.ARM.extab*) } > RRAM
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > RRAM

    /* -------- 3. Initialised data (LMA in RRAM, VMA in SRAM) ---------- */
    __data_load = LOADADDR(.data);
    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM AT > RRAM

    /* -------- 4. Zero-initialised BSS -------------------------------- */
    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SRAM

    /* -------- 5. Warm-reset survivor state (SRAM) ------------------- */
    /* `.uninit` and `.retained` (TIKU_RETAINED) are SRAM that zero-init
     * skips: they survive a warm reset and are lost on a power cycle.
     * `.persistent` (TIKU_DURABLE) is in RRAM, below, and survives power
     * loss. */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        *(.uninit*)
        . = ALIGN(32);
        __uninit_end = .;
        /* TIKU_RETAINED data. */
        *(.retained*)
    } > SRAM

    /* End of static allocations. Aliases for whichever name libc/kernel use. */
    _end = .;
    end = .;
    __end__ = .;
    __datastart = __data_start;

    /* SRAM headroom: the ARMv8-M MPU stack budget (32 KB reserve + 4 KB
     * RO guard, programmed in tiku_mpu_arch.c) must fit above the
     * statics. */
    ASSERT(_end <= __tiku_stack_guard_start,
           "ERROR: SRAM exhausted — need >= 36 KB (32K stack reserve + 4K MPU guard) below the stack")

    /* The SRAM tier takes the bank between the statics and the MPU stack
     * budget, with no extra reserve below the limit. */
    __tiku_tier_span_start      = ALIGN(_end, 32);
    __tiku_tier_span_limit      = __tiku_stack_guard_start;
    __tiku_tier_reserve_default = 0;
    __tiku_tier_floor_default   = 4K;
    INCLUDE arch/common/tiku_sram_layout.ld

    /* -------- 6. Durable persist (RRAM, byte-writable NVM) ---------- */
    /* `.persistent` sits in the persist partition at the top of RRAM,
     * written in place behind the RRAMC WEN gate, and survives a power
     * cycle.  NOLOAD keeps it out of the image: an ERASE_ALL leaves it
     * blank and the persist-cell gates prime their defaults at first boot.
     *
     * Every store into `.persistent` must sit inside
     * tiku_mpu_unlock_nvm()/lock_nvm() (kernel/memory/tiku_mem.h): with the
     * WEN gate closed the store is a precise bus fault. */
    .persistent __tiku_nvm_rram_start (NOLOAD) : ALIGN(4)
    {
        __persistent_start = .;
        /* TIKU_DURABLE_FIRST: the layout record opens the partition, at the
         * one offset every image agrees on; the rest follow link order. */
        KEEP(*(.persistent.layout))
        *(.persistent)
        . = ALIGN(4);
        __persistent_end = .;
    } > RRAM

    /* The RRAM image (code, rodata and the .data load copy) must end below
     * __tiku_code_limit: the code cap, less the module slot in builds with
     * the loader. */
    ASSERT(__data_load + (__data_end - __data_start) <= __tiku_code_limit,
           "ERROR: RRAM image overruns the reserved NVM/persist regions")

    /* The persist-partition data must fit inside the reserved span. */
    ASSERT(__persistent_end <= __tiku_nvm_rram_end,
           "ERROR: .persistent overruns the persist partition")

    /* The layout record opens the durable image in every build, so an image
     * with other durable variables still reads it where the last one wrote. */
    ASSERT((DEFINED(tiku_layout_pin) ? tiku_layout_pin : __persistent_start)
           == __persistent_start,
           "ERROR: tiku_layout_pin does not open .persistent (TIKU_DURABLE_FIRST holds one object)")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
