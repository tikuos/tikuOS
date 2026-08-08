/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * r7ka8p1kf.ld - R7KA8P1KF (EK-RA8P1) linker script, MRAM-resident image.
 *
 * The image lives in MRAM so a reset returns to it rather than to the factory
 * one, and `.persistent` sits in a reserved MRAM carve: measured, this MRAM is
 * byte-writable in place with no erase, so the durable grade needs no mirror.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

ENTRY(tiku_ra8p1_reset_handler)

MEMORY
{
    MRAM (rx)  : ORIGIN = 0x02000000, LENGTH = 1024K
    /* The first 16 KB belong to the CPU1 payload, which links at this fixed
     * address so its compiler-generated absolute loads are simply correct.
     * Keep in step with TIKU_CPU1_AREA_ADDR (tiku_cpu1_ipc.h) and the base
     * in arch/ra8p1/cpu1/tiku_cpu1.ld. */
    SRAM (rwx) : ORIGIN = 0x22004000, LENGTH = 1664K - 16K
}

/* "FRAM_*" vocabulary the kernel memory introspection speaks == MRAM here. */
__flash_start = ORIGIN(MRAM);
__flash_end   = ORIGIN(MRAM) + LENGTH(MRAM);
__sram_start  = ORIGIN(SRAM);
__sram_end    = ORIGIN(SRAM) + LENGTH(SRAM);

/*
 * Canonical NVM layout: inputs + the shared derivation (arch/common).
 *
 *   0x02000000  code            512 KB (largest image measured: 63 KB)
 *   0x02080000  module slot      32 KB (ARM-wide contract)
 *   0x02088000  NVM region      464 KB (tier + file store)
 *   0x020FC000  durable persist  16 KB (exact `.persistent`, in place)
 *   0x02100000  top
 *
 * Growing any of these MOVES every cell and file address, so an established
 * board re-primes once (/sys/persist reports primed != 0, /data empty).
 */
__tiku_layout_top          = ORIGIN(MRAM) + LENGTH(MRAM);
__tiku_layout_code_cap     = ORIGIN(MRAM) + 0x80000;        /* 512 KB code */
__tiku_layout_module_size  = 0x8000;                        /* 32 KB slot  */
__tiku_layout_durable_size = 0x4000;                        /* 16 KB persist */
INCLUDE arch/common/tiku_nvm_layout.ld

/* Platform aliases.  The name matches Ambiq's, the meaning does not: there
 * the MRAM span is a MIRROR that SRAM `.persistent` is flushed into, here it
 * is where the durable data actually lives and is written in place. */
__tiku_nvm_mram_size  = __tiku_layout_durable_size;
__tiku_nvm_mram_start = __tiku_layout_durable_start;
__tiku_nvm_mram_end   = __tiku_layout_top;

/* The Cortex-M loads the initial SP from vector[0]; place it at the top of
 * SRAM and let the stack grow down toward .bss. */
__stack = __sram_end;

SECTIONS
{
    /* -------- 1. Vectors (MRAM base -- this is the reset address) ----- */
    .vectors ORIGIN(MRAM) : ALIGN(512)
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        __vectors_end = .;
    } > MRAM

    /*
     * ALIGN(32) on every MPU boundary, not ALIGN(4).
     *
     * PMSAv8's region granule is 32 bytes: RBAR keeps base[31:5] and RLAR
     * keeps limit[31:5].  A boundary that is only 4-aligned cannot be
     * expressed, so the region would either overlap its neighbour (behaviour
     * the architecture leaves implementation-defined) or leave a hole that
     * falls through to the background map.  Aligning here is what lets the
     * W^X split be exact rather than approximate.
     */
    .text : ALIGN(32)
    {
        *(.text*)
        *(.glue_7) *(.glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
        *(.rodata*)
        *(.gnu.linkonce.r.*)
        . = ALIGN(32);
    } > MRAM

    /* ARM unwind tables: kept so a fault handler can walk the stack.  Without
     * an explicit output section the linker gives .ARM.exidx its own default
     * placement and it collides with .data's load address -- a link error, but
     * an obscure one to read. */
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
     * .data: VMA in SRAM, LMA in MRAM, and __data_load comes from LOADADDR
     * AFTERWARDS -- never from a captured `.`.
     *
     * Capturing `.` into __data_load and pinning the LMA to it is wrong the
     * moment the linker pads: .data aligns to 32, so if `.` is only 4-aligned
     * the VMA moves up while the LMA does not, the startup copy sees a src/dst
     * skew and shifts every initialised variable by the padding -- .data
     * silently corrupt, and only in builds whose alignment happened not to
     * work out.  `AT > MRAM` lets the linker choose and LOADADDR reports what
     * it chose, so the two can never drift apart.
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
    /* `.uninit` and the WARM grade sit outside .bss so the reset handler's
     * zero-fill skips them: they survive a warm reset and are reseeded on a
     * power cycle.  The DURABLE grade is not here: `.persistent` lives in
     * MRAM below and survives power loss too.
     *
     * TIKU_PERSIST_WARM is "never mirrored, never MPU-protected", and its
     * callers write it unbracketed on purpose -- tiku_hang_boot_init() clears
     * the hang record at every boot.  Sweep it into the protected window and
     * that write MemManage-faults before the shell ever starts. */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        *(.uninit*)
        . = ALIGN(32);
        *(.persistent.warm*)
        . = ALIGN(32);
        __uninit_end = .;
    } > SRAM

    _end = .;
    end = .;
    __end__ = .;
    PROVIDE(__heap_start = .);

    /*
     * SRAM TIER -- everything the statics did not take, less a fixed headroom.
     *
     * The tier is the only runtime way a program claims SRAM, so its size is
     * derived here rather than compiled in: a constant would have to be picked
     * per build configuration, because .bss differs by hundreds of KB between
     * an NPU build and a default one, and whatever number was chosen would
     * silently stop matching the moment a static buffer was added.
     *
     * Deriving it also keeps the span out of .bss, so the crt never zeroes it
     * -- the allocator does not promise zeroed memory (tiku_arena_alloc has no
     * memset) and at MOCO speeds zeroing a megabyte costs real boot time.
     *
     * TIKU_SRAM_HEADROOM is a small unallocated margin below the stack
     * budget, not a growth area for statics: those land in .bss below the
     * tier and shrink it rather than reaching here.  Nothing is placed in it,
     * so it is kept to the smallest figure that still separates the tier from
     * the stack's own reserve; the tier takes everything else.
     */
    /* Overridable so a link-contract test can squeeze the span from the top
     * without editing this file: --defsym=__tier_reserve_override=N.  DEFINED()
     * is required -- a bare --defsym symbol is not in scope for a script
     * expression and errors out as undefined. */
    TIKU_SRAM_HEADROOM = DEFINED(__tier_reserve_override)
                       ? __tier_reserve_override : 32K;
    __tier_sram_start = ALIGN(32);
    __tier_sram_end   = __stack - 0x8000 - TIKU_SRAM_HEADROOM;

    /* Keep the floor the BASIC arena is asserted against (TIKU_TIER_SRAM_MIN
     * on the make line).  Without this a large enough .bss would shrink the
     * tier below what BASIC carves and the failure would land at run time. */
    /* Must equal TIKU_TIER_SRAM_MIN for this MCU in the Makefile. */
    ASSERT((__tier_sram_end - __tier_sram_start) >= 0x40000,
           "ERROR: RA8P1 SRAM tier fell below its 256 KB floor -- .bss grew into it; shrink a static buffer or lower TIKU_SRAM_HEADROOM")

    /* SRAM headroom: the stack budget the MPU actually programs
     * (MPU_STACK_RESERVED_BYTES 32 KB + MPU_STACK_GUARD_BYTES 4 KB in
     * tiku_mpu_arch.c) must fit above the statics, so the figure below tracks
     * that sum.  Below the 32 KB reserve the guard region overlaps .uninit
     * and the link still succeeds. */
    ASSERT((__stack - _end) >= 0x9000,
           "ERROR: RA8P1 SRAM exhausted -- need >= 36 KB (32K stack reserve + 4K MPU guard) below the stack")

    /* -------- DURABLE persist (MRAM, byte-writable in place) --------- */
    /* NOLOAD: not in the programmed image, so a reflash leaves it alone and
     * a virgin part leaves it blank for the persist-cell magic gates to prime
     * on first boot.
     *
     * CONTRACT (enforced in hardware): every store into `.persistent` MUST be
     * bracketed by tiku_mpu_unlock_nvm()/lock_nvm().  Two gates stand behind
     * it here -- the MPU region is read-only outside the window, and MRAM
     * programming is disabled at MRCPC1.MRCPSEN.  Measured: with the second
     * one shut a store does not fall through silently, it HardFaults. */
    .persistent __tiku_nvm_mram_start (NOLOAD) : ALIGN(32)
    {
        __persistent_start = .;
        *(.persistent)
        . = ALIGN(32);
        __persistent_end = .;
    } > MRAM

    /* The MRAM image (code + rodata + .data load copy) must not grow up into
     * the reserved module slot, NVM region or persist home above it. */
    ASSERT(__data_load + (__data_end - __data_start) <= __tiku_module_slot,
           "ERROR: RA8P1 MRAM image overruns the reserved module/NVM/persist regions")

    /* The durable persist data must fit inside the reserved region. */
    ASSERT(__persistent_end <= __tiku_nvm_mram_end,
           "ERROR: durable .persistent overruns the reserved MRAM region")

    PROVIDE(__tiku_cpu1_area = 0x22000000);

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
