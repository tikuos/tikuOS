/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * rp2350.ld - RP2350 (Pico 2 / Pico 2 W) linker script.
 *
 * Places .boot2, the IMAGE_DEF block the boot ROM reads, and the Cortex-M33
 * vector table at the base of XIP flash, then the image; SRAM holds .data,
 * .bss, .uninit and a downward stack.  Map and exported symbols below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Memory map (RP2350 datasheet §2.2):
 *   FLASH (XIP)  : 4 MB external QSPI at 0x10000000
 *   SRAM         : 520 KB unified at 0x20000000
 *
 * Layout in flash:
 *   0x10000000 .boot2           (256 bytes: a placeholder word and padding;
 *                               the boot ROM sets up XIP itself)
 *   0x10000100 .image_def       (RP2350 IMAGE_DEF block, 28 bytes, which
 *                               the boot ROM reads to identify the image
 *                               and find the vector table)
 *   0x10000200 .vectors         (Cortex-M33 vector table, 512-byte aligned;
 *                               the reset handler is the first non-SP entry)
 *   0x10000xxx .text/.rodata    (rest of the image)
 *
 * Layout in SRAM:
 *   .data       (initialised RAM, copied at boot from .data LMA in flash)
 *   .bss        (zeroed at boot)
 *   .mpu_diag   (MPU fault record, not zeroed)
 *   .uninit     (durable state, not zeroed, mirrored to flash; .retained
 *                follows inside the same output section)
 *   SRAM tier   (the rest, up to the stack reserve)
 *   __stack     (top of SRAM; the stack grows down)
 *
 * Symbols exposed to the kernel:
 *   __stack         — top of stack (initial SP value, also used by
 *                     /sys/mem/free for stack-headroom calc)
 *   _end            — end of the static allocations (after .uninit)
 *   __sram_start    — base of SRAM region
 *   __sram_end      — top of SRAM region
 *   __flash_start   — base of flash region
 *   __flash_end     — top of flash region
 *   __data_load     — LMA of .data in flash (source for copy)
 *   __data_start    — VMA of .data in SRAM (dest for copy)
 *   __data_end      — end of .data in SRAM (one past last byte)
 *   __bss_start     — start of .bss
 *   __bss_end       — end of .bss
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(tiku_rp2350_reset_handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 4M
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 520K
}

__sram_start  = ORIGIN(SRAM);
__sram_end    = ORIGIN(SRAM) + LENGTH(SRAM);
__flash_start = ORIGIN(FLASH);
__flash_end   = ORIGIN(FLASH) + LENGTH(FLASH);

/* NVM backup sector: the top __tiku_layout_persist_size bytes of QSPI flash
 * (one 4 KB erase sector) hold the mirror of the SRAM .uninit region, which
 * tiku_mem_arch_nvm_flush_status() writes and tiku_mem_arch_init() restores,
 * so durable state survives a power cycle.  The boot-ROM flash helpers take
 * an offset from the start of flash, not an XIP address, and
 * __tiku_nvm_flash_offset is that offset.
 *
 * The size is a whole number of 4 KB sectors; the flush, the restore and the
 * .uninit ASSERT below all follow __tiku_layout_persist_size. */
/* Canonical NVM layout: inputs + shared derivation (arch/common). */
__tiku_layout_top          = ORIGIN(FLASH) + LENGTH(FLASH);
__tiku_layout_code_cap     = ORIGIN(FLASH) + 0x60000;      /* 384 KB code */
__tiku_layout_persist_size = 0x1000;                       /* 4 KB sector */
INCLUDE arch/common/tiku_nvm_layout.ld

__tiku_nvm_flash_size   = __tiku_layout_persist_size;
__tiku_nvm_flash_start  = __tiku_layout_persist_start;
__tiku_nvm_flash_end    = __tiku_layout_top;
__tiku_nvm_flash_offset = __tiku_layout_persist_start - ORIGIN(FLASH);

/* Carved NVM region: the QSPI flash between the 384 KB code window and the
 * backup sector, owned by the NVM tier and the /data file store through the
 * flash region backend (arch/arm-rp2350/tiku_nvm_region_rp2350.c).  From the
 * bottom: the NVM tier (32 KB by default, set by the nvm.tier layout knob),
 * then the /data store (TIKU_NVMFS_FS_BYTES), in whole 4 KB erase sectors.
 * Code grows up from ORIGIN(FLASH) inside the 384 KB window, and an ASSERT in
 * SECTIONS stops the flash image overrunning it.  A CYW43 build links the
 * CYW43439 firmware into .rodata, inside that window.
 *   __tiku_nvmfs_base   : XIP address (read in place by pointer), 0x10060000
 *   __tiku_nvmfs_offset : byte offset from flash start (for the boot ROM)
 *   __tiku_nvmfs_size   : absolute symbol -- its address is the size */
/* Module slot: the top 32 KB (8 erase sectors) of the code window, installed
 * sector by sector through the boot ROM with its header page programmed
 * last.  Its symbols come from the shared derivation above. */

/* __tiku_nvmfs_base/size come from the shared derivation; only the
 * boot-ROM byte offset is platform-local. */
__tiku_nvmfs_offset = __tiku_nvmfs_base - ORIGIN(FLASH);

/* The Cortex-M loads the initial SP from the first vector entry.
 * Place it at the very top of SRAM and let the stack grow downward. */
__stack       = __sram_end;

SECTIONS
{
    /* -------- 1. Boot2 (256 bytes, padded) --------------------------- */
    .boot2 ORIGIN(FLASH) :
    {
        KEEP(*(.boot2))
        . = ORIGIN(FLASH) + 256;     /* pad to exactly 256 bytes */
    } > FLASH

    /* -------- 2. RP2350 IMAGE_DEF + vectors -------------------------- */
    .image_def : ALIGN(4)
    {
        KEEP(*(.image_def))
    } > FLASH

    /* VTOR needs the table aligned to the smallest power of two at or
     * above its size, 128 bytes at least.  The table has 80 entries x 4 =
     * 320 bytes, so 512.  VTOR drops the low address bits, so a misaligned
     * table sends exceptions through the wrong vectors. */
    .vectors : ALIGN(512)
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        __vectors_end = .;
    } > FLASH

    /* -------- 3. Code & rodata --------------------------------------- */
    .text : ALIGN(4)
    {
        *(.text*)
        *(.glue_7) *(.glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > FLASH

    /* End of .text.  The `free` command and /sys/mem read _etext to size
     * the code in flash.  `etext` is the same address under the
     * traditional Unix name. */
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
    } > FLASH

    /* ARM exception-unwind tables.  tikuOS uses no C++ exceptions; library
     * objects can still contribute small entries. */
    .ARM.extab : { *(.ARM.extab*) } > FLASH
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH

    /* -------- 4. Initialised data ------------------------------------ */
    __data_load = LOADADDR(.data);
    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM AT > FLASH

    /* -------- 5. Zero-initialised BSS -------------------------------- */
    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SRAM

    /* MPU fault record: NOLOAD, so it survives a warm reset, and outside the
     * MPU-protected .uninit range, because the MemManage handler writes it
     * while .uninit is read-only.  It ends on a 32-byte boundary: MPU
     * regions start on 32-byte boundaries, and an unaligned .uninit would
     * make mpu_rbar_uninit() round its base down over the end of .mpu_diag,
     * where the handler's write would then fault. */
    .mpu_diag (NOLOAD) : ALIGN(32)
    {
        __mpu_diag_start = .;
        *(.mpu_diag*)
        . = ALIGN(32);
        __mpu_diag_end = .;
    } > SRAM

    /* Durable state, not zeroed at boot.  [__uninit_start, __uninit_end) is
     * mirrored to the flash backup sector and restored from it at boot, and
     * the MPU keeps it read-only outside an unlock window (tiku_mpu_arch.c).
     * ALIGN(32) puts the MPU region base exactly on __uninit_start. */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        /* TIKU_DURABLE_FIRST: the layout record opens the mirrored image, at
         * the one offset every image agrees on; the rest follow link order. */
        KEEP(*(.persistent.layout))
        *(.uninit*)
        /* DURABLE grade: input sections named exactly `.persistent` (the
         * boot counter, aliases, persist cells), mirrored to the flash
         * sector and MPU-protected. */
        *(.persistent)
        . = ALIGN(32);
        __uninit_end = .;
        /* RETAINED grade: NOLOAD SRAM after __uninit_end.  It survives a
         * warm reset but is neither mirrored to flash nor MPU-protected. */
        *(.retained*)
    } > SRAM

    /* End of the static allocations, under each name linked code uses:
     *   _end          — /sys/mem and the `free` command
     *   end           — newlib's libnosys _sbrk()
     *   __end__       — some libgloss variants
     * __datastart is the start of .data, for /sys/mem and `free`.
     */
    _end = .;
    end = .;
    __end__ = .;
    __datastart = __data_start;

    /*
     * SRAM tier: the SRAM between the static allocations and the stack
     * guard, less a reserve.  The linker places every static first, the
     * cert-TLS and CYW43 buffers among them, and the tier takes the rest.
     *
     * The span ends 0x9000 below __stack: the 32 KB stack reserve plus the
     * 4 KB guard below it, as mpu_stack_guard_base() in tiku_mpu_arch.c
     * computes.  A smaller value puts the tier on the guard.  The span lies
     * outside .bss, so the crt does not zero it.
     */
    /* The reserve is the margin between the tier's top and the guard: a
     * frame that strides past the 4 KB guard lands there before it reaches
     * the tier.  The fragment applies it, resolves the floor from the make
     * line and asserts both. */
    __tiku_tier_span_start      = ALIGN(32);
    __tiku_tier_span_limit      = __stack - 0x9000;
    __tiku_tier_reserve_default = 32K;
    __tiku_tier_floor_default   = 0x40000;
    INCLUDE arch/common/tiku_sram_layout.ld

    /* Static data must end below the MPU's stack and guard reservation.
     * tiku_mpu_arch.c keeps the top MPU_STACK_RESERVED_BYTES (32 KB) for the
     * stack, puts a MPU_STACK_GUARD_BYTES (4 KB) guard directly below it, and
     * programs SEG2b (SRAM_MID) as [__uninit_end, guard_base).  With _end
     * less than 36 KB below __stack, the guard would cover live data, where
     * every write faults, and SRAM_MID would invert (base > limit).
     * 0x9000 = 32768 + 4096: change it together with those two C constants
     * and their copies in TikuBench tests/memory/test_mem_mpu.c. */
    ASSERT((__stack - _end) >= 0x9000,
           "ERROR: SRAM exhausted — need >= 36 KB free for the MPU stack + guard region")

    /* .uninit, which the backup sector mirrors, must fit the backup area
     * (__tiku_nvm_flash_size) less the mirror's 16-byte header. */
    ASSERT((__uninit_end - __uninit_start) <= (__tiku_nvm_flash_size - 16),
           "ERROR: .uninit region exceeds the 4 KB flash backup sector minus its 16-byte V2 header")

    /* tiku_layout_pin, when linked, must be the first object in .uninit:
     * every image reads the layout record at that offset, whatever other
     * durable variables it holds. */
    ASSERT((DEFINED(tiku_layout_pin) ? tiku_layout_pin : __uninit_start)
           == __uninit_start,
           "ERROR: tiku_layout_pin does not open .uninit (TIKU_DURABLE_FIRST holds one object)")

    /* The flash image (code + rodata + the .data load copy) must not grow up
     * into the carved NVM region below the backup sector. */
    ASSERT(__data_load + (__data_end - __data_start) <= __tiku_code_limit,
           "ERROR: flash image overruns the carved NVM region")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
