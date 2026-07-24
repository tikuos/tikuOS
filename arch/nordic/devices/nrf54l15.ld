/*
 * Tiku Operating System v0.05 — Nordic nRF54L15 (nRF54L15-DK) linker script
 *
 * Memory map (nRF54L15 datasheet):
 *   RRAM  : 1.5 MB non-volatile, write-in-place, at 0x00000000
 *   SRAM  : 256 KB at 0x20000000
 *
 * Layout in RRAM (grows up from 0x0):
 *   0x00000000 .vectors     (Cortex-M33 vector table; the CPU loads SP from
 *                            word 0 and the reset PC from word 1 -- so the
 *                            table MUST sit at the RRAM base and be aligned
 *                            to the VTOR requirement, which 0x0 satisfies)
 *   0x00000xxx .text/.rodata (code + read-only data)
 *   0x000xxxxx .data (LMA)   (initialised-data image, copied to SRAM at boot)
 *   ...        <free RRAM>    (code budget grows toward the reserved NVM tail)
 *   top-40 KB  reserved      (two byte-writable NVM regions written behind the
 *                            RRAMC WEN gate: a 32 KB filestore -- the
 *                            tiku_nvm_region backend / TFS /data / nvmprobe --
 *                            and, at the very top, an 8 KB durable-persist
 *                            region holding exact `.persistent`; symbols below)
 *
 * Layout in SRAM (stack grows down from the top):
 *   .data              (initialised RAM, copied from its RRAM LMA at boot)
 *   .bss               (zeroed at boot)
 *   .uninit/.persistent(skipped by zero-init: warm-reset survivor state)
 *   .persistent.warm   (warm-survivor SRAM, outside any future NVM mirror)
 *   __stack            (top of SRAM, grows downward)
 *
 * Symbols exposed to the kernel mirror the other ARM ports (see rp2350.ld):
 *   __stack __data_load __data_start __data_end __bss_start __bss_end
 *   __uninit_start __uninit_end _end end __end__ __datastart
 *   __sram_start __sram_end __flash_start __flash_end   (FRAM_* vocabulary)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(tiku_nordic_reset_handler)

MEMORY
{
    /* Usable application RRAM is 0x17D000 (1524 KB), NOT the full 1536 KB --
     * the top 12 KB is reserved (per the MDK NRF_MEMORY_FLASH_SIZE and nrfx's
     * own linker script).  Addressing past 0x17CFFF bus-faults. */
    RRAM (rx)  : ORIGIN = 0x00000000, LENGTH = 0x17D000
    /* App SRAM is 240 KB of the 256 KB part: the top 16 KB
     * (0x2003C000..0x2003FFFF) is the FLPR coprocessor carve -- RISC-V
     * code + stack + the shared IPC page (arch/nordic/flpr/tiku_flpr_ipc.h
     * is the geometry's single source of truth).  Reserved UNCONDITIONALLY
     * so image layout does not shift with TIKU_FLPR_ENABLE; the largest
     * build today (shell+BASIC) uses ~105 KB of .bss, leaving >2x headroom. */
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x3C000
}

/* "FRAM_*" vocabulary the kernel memory introspection speaks == RRAM here. */
__flash_start = ORIGIN(RRAM);
__flash_end   = ORIGIN(RRAM) + LENGTH(RRAM);
__sram_start  = ORIGIN(SRAM);
__sram_end    = ORIGIN(SRAM) + LENGTH(SRAM);

/* Reserved durable-persist region at the top of RRAM (byte-writable NVM
 * behind the RRAMC WEN gate; exact `.persistent` lands here, durable in
 * place).  16 KB: persist cells + aliases + lc + inittab + headroom.
 * NOTE (2026-07-15 Ambiq-parity layout, kintsugi/memoryfix.md B.0): growing
 * these carves MOVES every cell/TFS address -- established DKs re-prime once
 * (primed != 0, /data empty).  Expected, one-time. */
__tiku_nvm_rram_size  = 0x4000;                             /* 16 KB */
__tiku_nvm_rram_start = ORIGIN(RRAM) + LENGTH(RRAM) - __tiku_nvm_rram_size;
__tiku_nvm_rram_end   = ORIGIN(RRAM) + LENGTH(RRAM);

/* Carved NVM region (the tiku_nvm_region backend), Ambiq-style layout:
 * NVM tier at the front | TFS FS extent (256 KB, TIKU_NVMFS_FS_BYTES) |
 * reserved named-slot tail (64 KB, TIKU_NVM_RESERVED_BYTES: BASIC saved
 * program + F1 run-state checkpoint).  A byte-writable RRAM span just BELOW
 * the durable-persist region; written in place behind the WEN gate (no
 * erase).  740 KB leaves a 768 KB code window below (image today ~110 KB;
 * the ASSERT at the bottom is the hard cap).  __tiku_nvmfs_size is an
 * absolute symbol whose ADDRESS is the size (matches rp2350/ambiq). */
/* Loadable-native-module slot (Tier 3, tiku_basic_module.h): 32 KB of
 * byte-writable, EXECUTABLE RRAM at the TOP OF THE CODE WINDOW -- canonical
 * layout order (2026-07): code | module | NVM region | persist.  Keep in
 * sync with TIKU_MODULE_CARVE_ADDR (0x0B8000). */
__tiku_module_slot_size = 0x8000;                           /* 32 KB */
__tiku_module_slot      = 0x0B8000;                         /* code hard cap */

__tiku_nvmfs_size = 0xB9000;                                /* 740 KB */
__tiku_nvmfs_base = __tiku_module_slot + __tiku_module_slot_size;
__tiku_nvmfs_end  = __tiku_nvm_rram_start;

/* The Cortex-M loads the initial SP from vector[0]; place it at the top of
 * SRAM and let the stack grow down toward .bss. */
__stack = __sram_end;

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
    /* The WARM grade (`.persistent.warm` + the net stack's transient pools)
     * plus `.uninit` live in SRAM: skipped by zero-init, survive a warm reset,
     * reseeded on a power cycle.  The DURABLE grade (exact `.persistent`) is
     * NOT here -- it lives in RRAM below and survives power loss too. */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        *(.uninit*)
        . = ALIGN(32);
        __uninit_end = .;
        /* WARM grade only (EXCLUDE_FILE net routing retired: net kits
         * place their buffers explicitly at the declarations). */
        *(.persistent.warm*)
    } > SRAM

    /* End of static allocations. Aliases for whichever name libc/kernel use. */
    _end = .;
    end = .;
    __end__ = .;
    __datastart = __data_start;

    /* SRAM headroom: the ARMv8-M MPU stack budget (32 KB reserve + 4 KB
     * RO guard, programmed in tiku_mpu_arch.c) must fit above the statics
     * -- rp2350-parity sizing. */
    ASSERT((__stack - _end) >= 0x9000,
           "ERROR: SRAM exhausted — need >= 36 KB (32K stack reserve + 4K MPU guard) below the stack")

    /* -------- 6. DURABLE persist (RRAM, byte-writable NVM) ---------- */
    /* Exact `.persistent` lives at the reserved top of RRAM, written behind the
     * RRAMC WEN gate (tiku_mpu_unlock_nvm/lock_nvm).  Unlike the SRAM-backed
     * .persistent on rp2350/ambiq, this survives a POWER CYCLE, not just a warm
     * reset -- the distinctive nRF54L durability (RRAM is byte-writable NVM).
     * NOLOAD: not in the flash image, so an ERASE_ALL leaves it virgin and the
     * persist-cell magic gates prime defaults on first boot; a reset or power
     * cycle preserves it.
     *
     * CONTRACT (enforced in hardware here): every store into `.persistent`
     * MUST be bracketed by tiku_mpu_unlock_nvm()/lock_nvm().  On MSP430 an
     * unbracketed store is silently dropped by the FRAM MPU; on nRF54L it is
     * a PRECISE BUS FAULT (RRAMC WEN closed) -- this port is the loud canary
     * for the rule in kernel/memory/tiku_mem.h. */
    .persistent __tiku_nvm_rram_start (NOLOAD) : ALIGN(4)
    {
        __persistent_start = .;
        *(.persistent)
        . = ALIGN(4);
        __persistent_end = .;
    } > RRAM

    /* The RRAM code image (code + rodata + .data load copy) must not grow up
     * into the reserved NVM regions at the top of RRAM -- the filestore sits
     * just below the durable-persist region, so guard against the lower of
     * the two bases. */
    ASSERT(__data_load + (__data_end - __data_start) <= __tiku_module_slot,
           "ERROR: RRAM image overruns the reserved NVM/persist regions")

    /* The durable persist data must fit inside the reserved region. */
    ASSERT(__persistent_end <= __tiku_nvm_rram_end,
           "ERROR: durable .persistent overruns the reserved RRAM region")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
