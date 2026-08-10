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
 *   0x02000000  code            384 KB (largest image measured: 79 KB)
 *   0x02060000  NVM region      624 KB (32 KB tier + 592 KB file store)
 *   0x020FC000  persist partition 16 KB (exact `.persistent`, in place)
 *   0x02100000  top
 *
 * Growing any of these MOVES every cell and file address, so an established
 * board re-primes once (/sys/persist reports primed != 0, /data empty).
 */
__tiku_layout_top          = ORIGIN(MRAM) + LENGTH(MRAM);
__tiku_layout_code_cap     = ORIGIN(MRAM) + 0x60000;        /* 384 KB code */
__tiku_layout_persist_size = 0x4000;                        /* 16 KB partition */
INCLUDE arch/common/tiku_nvm_layout.ld

/* Platform aliases.  The name matches Ambiq's, the meaning does not: there
 * the MRAM span is a MIRROR that SRAM `.persistent` is flushed into, here it
 * is where the durable data actually lives and is written in place. */
__tiku_nvm_mram_size  = __tiku_layout_persist_size;
__tiku_nvm_mram_start = __tiku_layout_persist_start;
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
    /* `.uninit` and the RETAINED grade sit outside .bss so the reset handler's
     * zero-fill skips them: they survive a warm reset and are reseeded on a
     * power cycle.  The DURABLE grade is not here: `.persistent` lives in
     * MRAM below and survives power loss too.
     *
     * TIKU_RETAINED is "never mirrored, never MPU-protected", and its
     * callers write it unbracketed on purpose -- tiku_hang_boot_init() clears
     * the hang record at every boot.  Sweep it into the protected window and
     * that write MemManage-faults before the shell ever starts. */
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
     * TIKU_SRAM_HEADROOM is the margin between the tier's top and the stack
     * machinery: a stack frame striding past the 4 KB guard lands here, in
     * unowned memory, before it lands on the tier.  No frame in the tree
     * strides past the guard (arch/nordic/tiku_mpu_arch.c sizes both from
     * BASIC's largest frames), so the figure is that same worst frame with
     * margin -- defence for a frame class the tree does not yet contain,
     * not a measured need.
     */
    /* The span limit is the stack top less the MPU stack budget (0x8000,
     * MPU_STACK_RESERVED_BYTES in tiku_mpu_arch.c); the fragment applies the
     * reserve, resolves the floor from the make line and asserts both. */
    __tiku_tier_span_start      = ALIGN(32);
    __tiku_tier_span_limit      = __stack - 0x8000;
    __tiku_tier_reserve_default = 32K;
    __tiku_tier_floor_default   = 0x40000;
    INCLUDE arch/common/tiku_sram_layout.ld

    /* SRAM headroom: the stack budget the MPU actually programs
     * (MPU_STACK_RESERVED_BYTES 32 KB + MPU_STACK_GUARD_BYTES 4 KB in
     * tiku_mpu_arch.c) must fit above the statics, so the figure below tracks
     * that sum.  Below the 32 KB reserve the guard region overlaps .uninit
     * and the link still succeeds. */
    ASSERT((__stack - _end) >= 0x9000,
           "ERROR: RA8P1 SRAM exhausted -- need >= 36 KB (32K stack reserve + 4K MPU guard) below the stack")

    /* -------- Persist partition (MRAM, byte-writable in place) ------- */
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
     * the NVM region or persist partition above it -- nor into the Tier-3 module
     * slot at the top of the window, in builds that reserve one. */
    ASSERT(__data_load + (__data_end - __data_start) <= __tiku_code_limit,
           "ERROR: RA8P1 MRAM image overruns the code window (or its module reserve)")

    /* The persist-partition data must fit inside the reserved span. */
    ASSERT(__persistent_end <= __tiku_nvm_mram_end,
           "ERROR: .persistent overruns the persist partition")

    PROVIDE(__tiku_cpu1_area = 0x22000000);

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
