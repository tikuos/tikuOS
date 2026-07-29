/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * nrf54lm20a.ld - Nordic nRF54LM20A (nRF54LM20-DK) linker script.
 *
 * Cloned from nrf54l15.ld; the differences are the larger RRAM, the RAM2
 * upper bank that backs the tier arena, and nothing else -- the FLPR carve
 * and reserved-NVM geometry are identical.  Map and symbols below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Memory map (nRF54LM20A MDK):
 *   RRAM  : ~2 MB non-volatile, write-in-place, at 0x00000000
 *   SRAM  : 512 KB at 0x20000000 (RAM 256 KB + RAM2 256 KB, contiguous)
 *
 * Cloned from nrf54l15.ld; the differences are the two region LENGTHs (larger
 * RRAM) and the RAM2 upper bank (SRAM2, the tier arena).  The FLPR SRAM carve
 * (top 16 KB of the LOWER bank) is identical to the L15 -- same VPR core, same
 * carve address on every nordic part.  The reserved-NVM geometry at the top of
 * RRAM (32 KB filestore + 8 KB durable persist) is identical to the L15.
 *
 * Layout in RRAM (grows up from 0x0):
 *   0x00000000 .vectors     (Cortex-M33 vector table at the RRAM base, VTOR=0)
 *   0x00000xxx .text/.rodata (code + read-only data)
 *   0x000xxxxx .data (LMA)   (initialised-data image, copied to SRAM at boot)
 *   ...        <free RRAM>    (code budget grows toward the reserved NVM tail)
 *   top-40 KB  reserved      (32 KB filestore + 8 KB durable-persist, both
 *                            byte-writable behind the RRAMC WEN gate)
 *
 * Layout in SRAM (stack grows down from the top):
 *   .data / .bss / .uninit / .persistent.warm / __stack
 *
 * Symbols exposed to the kernel mirror the L15 script and the other ARM ports.
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(tiku_nordic_reset_handler)

MEMORY
{
    /* Usable application RRAM is 0x1FD000 (2036 KB), NOT the full 2 MB -- the
     * top 12 KB is reserved (per the MDK NRF_MEMORY_FLASH_SIZE and nrfx's own
     * linker script).  Addressing past 0x1FCFFF bus-faults. */
    RRAM (rx)  : ORIGIN = 0x00000000, LENGTH = 0x1FD000
    /* The LM20A has 512 KB SRAM in two banks -- RAM (256 KB @ 0x20000000) and
     * RAM2 (256 KB @ 0x20040000).  The LOWER bank is the primary bank: image
     * data, .uninit and the stack (at its top) live here, matching Nordic's own
     * nrf_common.ld, which sets __stack at the top of the lower bank and
     * declares the upper bank as a SEPARATE opt-in region.
     *
     * App SRAM is 240 KB of the lower bank: the top 16 KB (0x2003C000..
     * 0x2003FFFF) is the FLPR (VPR RISC-V) coprocessor carve -- code + stack +
     * the shared IPC page (arch/nordic/flpr/tiku_flpr_ipc.h), identical to the
     * L15 (same VPR base + carve address on every nordic part).  Reserved
     * unconditionally so the image layout does not shift with TIKU_FLPR_ENABLE;
     * the RAM2 tier arena (SRAM2 below) is untouched by it. */
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x3C000
    /* RAM2 holds large buffers (the .ram2 section: SRAM tier arena).  The TOP
     * OF THE BANK IS NOT FULLY BACKED on this silicon: a boot stack placed at
     * 0x20080000 bus-faults on the first push (STKERR, BFAR=0x2007FFF0), and a
     * 256 B-step CPU write probe faults first at 0x2007FF00 (0x2007FExx ok) --
     * both measured on the LM20-DK (nRF54LM20B eng sample).  The MDK claims
     * the full 0x40000; reserve the top 1 KB for margin -> 255 KB usable.
     * Keep in sync with TIKU_DEVICE_RAM2_SIZE in tiku_device_nrf54lm20a.h. */
    SRAM2 (rw) : ORIGIN = 0x20040000, LENGTH = 0x3FC00
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
/* Canonical NVM layout: inputs + shared derivation (arch/common). */
__tiku_layout_top          = ORIGIN(RRAM) + LENGTH(RRAM);
/* PACKING-ONLY ESCAPE HATCH -- not a tuning knob.  tools/axonpack.py's
 * verification gate compares its patched output against a LINKED image of the
 * same model, and the model that most needs checking (tinyml_vww, 698 KB) is
 * precisely the one that cannot link.  Passing
 * -Wl,--defsym=__tiku_code_cap_override=0xC0000 produces a reference ELF for
 * that comparison only; it is never flashed and never shipped, and
 * tools/axonpack_selftest.sh is the only caller.  A build that needs this to
 * produce FIRMWARE has exactly the pathology the window exists to prevent --
 * see the budget note below before reaching for it. */
__tiku_layout_code_cap     = DEFINED(__tiku_code_cap_override)
                             ? __tiku_code_cap_override
                             : 0x60000;                     /* 384 KB code */
/* Shared Nordic code budget: 384 KB on BOTH parts -- the same figure every
 * TikuOS platform now uses (P4), so "does this firmware fit" has ONE answer for
 * the whole fleet, and the module slot lands at the same address on both Nordic
 * parts (one module image serves each).
 *
 * Sizing this window by the largest image anyone can build lands it on an Axon
 * KAT: Nordic's compiled model weights and test vectors are const arrays, so
 * they go to .rodata, so they land HERE (measured 2026-07-25, lm20b, -Os):
 *
 *   shell + BASIC, no Axon .............................  126 KB
 *   + Axon driver, model gc-dropped ....................  127 KB
 *   + tinyml_kws  weights  25 KB + vectors  72 KB ......  206 KB
 *   + tinyml_ic   weights                       .......  357 KB
 *   + tinyml_ad   weights 260 KB + vectors   5 KB ......  362 KB
 *   + tinyml_vww  weights 203 KB + vectors 307 KB ......  698 KB  <-- set the cap
 *
 * Letting that set the cap would put 544 KB of RRAM permanently beyond reach on
 * every shipped board so a vendor TEST HARNESS could link, against an actual
 * firmware of 126 KB.
 *
 * So model weights are DATA and live in /data:
 * TIKU_AXON_MODEL_FROM_STORE=1 compiles no model at all and reads each one --
 * weights, command buffer, descriptor and labels -- from a file at run time.
 * That image is 91 KB, and it is 91 KB whichever model it runs, so the biggest
 * model no longer has any say in this number.  All four shipped models,
 * tinyml_vww included, run from it bit-exact against the vendor's own vectors.
 * TIKU_AXON_MODEL=<name> still bakes one in for development and remains the
 * reference the store path is diffed against; vww still cannot link that way,
 * which is now a statement about the harness rather than a limitation.
 *
 * Do not raise this number to make something build.
 * The L15 has no NPU, so it only ever needs ~140 KB; it carries the same cap
 * for family consistency, and the address space it gives up was idle anyway
 * (the freed NVM goes to the durable tier, which currently has no consumer). */
__tiku_layout_module_size  = 0x8000;                        /* 32 KB slot  */
__tiku_layout_durable_size = 0x4000;                        /* 16 KB persist */
INCLUDE arch/common/tiku_nvm_layout.ld

__tiku_nvm_rram_size  = __tiku_layout_durable_size;
__tiku_nvm_rram_start = __tiku_layout_durable_start;
__tiku_nvm_rram_end   = __tiku_layout_top;

/* Carved NVM region (the tiku_nvm_region backend), Ambiq-style layout:
 * NVM tier at the front (32 KB) | TFS FS extent (all the rest,
 * TIKU_NVMFS_FS_BYTES -- BASIC's saved program and run-state checkpoint are
 * files in it, not a carved tail).  A byte-writable RRAM span just BELOW
 * the durable-persist region; written in place behind the WEN gate (no
 * erase).  The 384 KB code window ends at the module slot (the ASSERT at the
 * bottom is the hard cap).
 * __tiku_nvmfs_size is an absolute symbol whose ADDRESS is the size. */
/* Loadable-native-module slot (Tier 3, tiku_basic_module.h): 32 KB of
 * byte-writable, EXECUTABLE RRAM at the TOP OF THE CODE WINDOW -- canonical
 * layout order (2026-07): code | module | NVM region | persist.  Modules are
 * code, so slot growth trades against the code estate on every platform.
 * The loader writes the image in place behind the WEN gate and runs it XIP
 * (durable across reboot and power loss); SRAM is W^X (execute-never), so a
 * module MUST live in RRAM.  Keep in sync with TIKU_MODULE_CARVE_ADDR. */
/* (module slot + NVM region symbols come from the shared derivation.) */

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

    /* End of static allocations in the PRIMARY bank (the .ram2 section below
     * lives in SRAM2 and must not disturb these markers -- _end feeds the
     * stack-headroom ASSERT and libc's notion of the heap). */
    _end = .;
    end = .;
    __end__ = .;
    __datastart = __data_start;

    /* SRAM headroom: the ARMv8-M MPU stack budget (32 KB reserve + 4 KB
     * RO guard, programmed in tiku_mpu_arch.c) must fit above the statics
     * -- rp2350-parity sizing. */
    ASSERT((__stack - _end) >= 0x9000,
           "ERROR: SRAM exhausted — need >= 36 KB (32K stack reserve + 4K MPU guard) below the stack")

    /* -------- 5b. RAM2 large buffers (upper SRAM bank) ---------------- */
    /* NOLOAD: nothing to copy from the image; the crt zeroes exactly
     * [__ram2_start, __ram2_end) at boot (like .bss -- the tier arena
     * expects zeroed memory), leaving the rest of the bank untouched. */
    .ram2 (NOLOAD) : ALIGN(8)
    {
        __ram2_start = .;
        *(.ram2*)
        . = ALIGN(4);
        __ram2_end = .;
    } > SRAM2

    /* -------- 6. DURABLE persist (RRAM, byte-writable NVM) ---------- */
    /* Exact `.persistent` lives at the reserved top of RRAM, written behind the
     * RRAMC WEN gate (tiku_mpu_unlock_nvm/lock_nvm).  This survives a POWER
     * CYCLE, not just a warm reset -- the distinctive nRF54L durability (RRAM
     * is byte-writable NVM).  NOLOAD: not in the flash image, so an ERASE_ALL
     * leaves it virgin and the persist-cell magic gates prime defaults on first
     * boot; a reset or power cycle preserves it.
     *
     * CONTRACT (enforced in hardware here): every store into `.persistent`
     * MUST be bracketed by tiku_mpu_unlock_nvm()/lock_nvm().  On nRF54L an
     * unbracketed store is a PRECISE BUS FAULT (RRAMC WEN closed) -- this port
     * is the loud canary for the rule in kernel/memory/tiku_mem.h. */
    .persistent __tiku_nvm_rram_start (NOLOAD) : ALIGN(4)
    {
        __persistent_start = .;
        *(.persistent)
        . = ALIGN(4);
        __persistent_end = .;
    } > RRAM

    /* The RRAM code image (code + rodata + .data load copy) must not grow up
     * into the reserved NVM regions at the top of RRAM -- guard against the
     * lower of the two bases (the filestore). */
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
