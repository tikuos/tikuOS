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
 *   top-8 KB   reserved      (future durable persist/config region -- symbols
 *                            defined below; RRAMC-backed backend lands later)
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
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

/* "FRAM_*" vocabulary the kernel memory introspection speaks == RRAM here. */
__flash_start = ORIGIN(RRAM);
__flash_end   = ORIGIN(RRAM) + LENGTH(RRAM);
__sram_start  = ORIGIN(SRAM);
__sram_end    = ORIGIN(SRAM) + LENGTH(SRAM);

/* Reserved durable-persist region at the top of RRAM.  The RRAMC-backed
 * backend (byte-writable NVM behind the WEN gate) is added in a later phase;
 * the symbols are defined now so the region's addresses stay stable and the
 * code-image ASSERT below can guard against overrunning it. */
__tiku_nvm_rram_size  = 0x2000;                             /* 8 KB */
__tiku_nvm_rram_start = ORIGIN(RRAM) + LENGTH(RRAM) - __tiku_nvm_rram_size;
__tiku_nvm_rram_end   = ORIGIN(RRAM) + LENGTH(RRAM);

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

    /* -------- 5. Reboot-survivor state (SRAM) ----------------------- */
    /* Both persist grades live in SRAM for now: `.uninit` + exact `.persistent`
     * (DURABLE grade) + `.persistent.warm` (WARM grade).  NOLOAD -> skipped by
     * zero-init, survive a WARM reset, reseeded on a power cycle.
     *
     * NOTE: an attempt to move the DURABLE `.persistent` grade into RRAM (for
     * true power-cycle durability -- the distinctive nRF54L angle) hit a precise
     * bus fault on the first runtime RRAM store, even at a valid address with
     * the RRAMC WEN gate open (matches the nrfx write path).  nRF54L has no
     * instruction cache, so the leading hypothesis is write-while-executing-
     * from-RRAM; the fix is likely a RAM-resident persist-write routine.  A
     * focused follow-up owns that; persist stays SRAM-backed here so the board
     * boots.  The reserved RRAM region + __tiku_nvm_rram_* symbols are kept. */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        *(.uninit*)
        *(EXCLUDE_FILE(*tiku_kits_net_*.o) .persistent)
        . = ALIGN(32);
        __uninit_end = .;
        *(.persistent.warm*)
        *tiku_kits_net_*.o(.persistent*)
    } > SRAM

    /* End of static allocations. Aliases for whichever name libc/kernel use. */
    _end = .;
    end = .;
    __end__ = .;
    __datastart = __data_start;

    /* SRAM headroom: keep at least 8 KB free below the descending stack so
     * a deep call chain cannot collide with live static data.  (The ARMv8-M
     * MPU stack-guard budget of the rp2350 port is larger; nordic's MPU
     * region programming lands in a later phase, so guard conservatively.) */
    ASSERT((__stack - _end) >= 0x2000,
           "ERROR: SRAM exhausted — need >= 8 KB free below the stack")

    /* The RRAM code image (code + rodata + .data load copy) must not grow up
     * into the reserved durable-persist region at the top of RRAM (reserved
     * now, wired to a RAM-resident RRAM writer in a follow-up). */
    ASSERT(__data_load + (__data_end - __data_start) <= __tiku_nvm_rram_start,
           "ERROR: RRAM image overruns the reserved durable-persist region")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
