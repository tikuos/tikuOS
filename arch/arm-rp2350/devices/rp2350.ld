/*
 * Tiku Operating System v0.05 — RP2350 (Pico 2 / Pico 2 W) linker script
 *
 * Memory map (RP2350 datasheet §2.2):
 *   FLASH (XIP)  : 4 MB external QSPI at 0x10000000
 *   SRAM         : 520 KB unified at 0x20000000
 *
 * Layout in flash:
 *   0x10000000 .boot2          (first 256 bytes; loaded by ROM into SRAM
 *                              and executed before XIP is alive)
 *   0x10000100 .image_def       (RP2350 IMAGE_DEF block — 16 bytes — read
 *                              by the boot ROM to verify image and pick
 *                              entry point)
 *   0x10000110 .vectors         (Cortex-M33 vector table; reset handler
 *                              is the first non-SP entry)
 *   0x10000xxx .text/.rodata    (rest of the image)
 *
 * Layout in SRAM:
 *   .data       (initialised RAM, copied at boot from .data LMA in flash)
 *   .bss        (zeroed at boot)
 *   .uninit     (skipped by zero-init — used for boot-counter persistence)
 *   __stack     (top of SRAM grows down toward .bss)
 *
 * Symbols exposed to the kernel:
 *   __stack         — top of stack (initial SP value, also used by
 *                     /sys/mem/free for stack-headroom calc)
 *   _end            — end of .bss (start of free SRAM heap)
 *   __sram_start    — base of SRAM region
 *   __sram_end      — top of SRAM region
 *   __flash_start   — base of flash region
 *   __flash_end     — top of flash region
 *   __data_load     — LMA of .data in flash (source for copy)
 *   __data_start    — VMA of .data in SRAM (dest for copy)
 *   __data_end      — end of .data in SRAM (one past last byte)
 *   __bss_start     — start of .bss
 *   __bss_end       — end of .bss
 *
 * SPDX-License-Identifier: Apache-2.0
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

/* NVM backup sector: the last 4 KB of QSPI flash is reserved for
 * tiku_mem_arch_nvm_write() to mirror the SRAM .uninit region.  This
 * gives "persistent" state real power-cycle durability instead of
 * relying on .uninit's warm-reset-only survival.  The boot-ROM flash
 * helpers use the flash OFFSET (from start of QSPI), not the absolute
 * XIP address; __tiku_nvm_flash_offset is that offset for convenience.
 *
 * Size matches one QSPI erase granule (4 KB).  The current .uninit
 * region is ~3 KB; growing past 4 KB will require multi-sector
 * backup which the arch driver does NOT support today. */
__tiku_nvm_flash_size   = 0x1000;             /* 4 KB = one sector */
__tiku_nvm_flash_offset = LENGTH(FLASH) - __tiku_nvm_flash_size;
__tiku_nvm_flash_start  = ORIGIN(FLASH) + __tiku_nvm_flash_offset;
__tiku_nvm_flash_end    = ORIGIN(FLASH) + LENGTH(FLASH);

/* Carved NVM region (substrate B): a ~3 MB slice of QSPI flash just below the
 * 4 KB .uninit backup sector, owned by the /data file store + NVM tier via the
 * Flash region backend (arch/arm-rp2350/tiku_nvm_region_rp2350.c).  Sub-layout
 * (front->top): NVM tier extent (~124 KB, durable TIKU_MEM_NVM) | /data FS
 * extent (TIKU_NVMFS_FS_BYTES, 2.75 MB) | reserved tail (TIKU_NVM_RESERVED_BYTES,
 * 128 KB durable named data).  4 KB-sector aligned (the flash erase granule).
 * Code grows up from ORIGIN(FLASH) into the remaining 1 MB; an ASSERT in
 * SECTIONS guards the flash image against overrunning it.
 *   __tiku_nvmfs_base   : XIP address (read in place by pointer); == 0x10100000
 *   __tiku_nvmfs_offset : byte offset from flash start (for the boot-ROM)
 *   __tiku_nvmfs_size   : ABSOLUTE symbol -- its ADDRESS is the size */
__tiku_nvmfs_size   = 0x2FF000;     /* 3068 KB; leaves a 1 MB code budget */
__tiku_nvmfs_offset = __tiku_nvm_flash_offset - __tiku_nvmfs_size;
__tiku_nvmfs_base   = ORIGIN(FLASH) + __tiku_nvmfs_offset;

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

    /* The Cortex-M33 VTOR alignment requirement is the smallest
     * power-of-2 that's >= total-table-size-in-bytes, with a 128-byte
     * minimum. Our table has 80 entries × 4 = 320 bytes -> 512-byte
     * alignment required. A misaligned VTOR silently truncates the
     * low bits, sending exception handlers to garbage. */
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

    /* End of .text. tikuOS's `free`/ /sys/mem/free use _etext to size the
     * FRAM-resident (here flash-resident) code region. Standard newlib name;
     * `etext` aliased for code that uses the historic spelling. */
    _etext = .;
    etext  = .;

    .rodata : ALIGN(4)
    {
        *(.rodata*)
        *(.gnu.linkonce.r.*)
    } > FLASH

    /* C++/static-ctor tables (not used by tikuOS but linker may emit
     * tiny stubs from libc we don't link). */
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

    /* MPU diagnostic state — lives just before .uninit so it
     * survives warm reset (NOLOAD) but sits OUTSIDE the MPU-protected
     * range. The MemManage handler needs to write to its own
     * persistent state across the reset it triggers; if that state
     * lived in .uninit it would itself fault.
     *
     * IMPORTANT: pad to a 32-byte boundary at the end so the next
     * section (.uninit) starts naturally 32-byte aligned. ARMv8-M MPU
     * regions snap to 32-byte boundaries; if .uninit started on a
     * non-32-aligned address, mpu_rbar_uninit() would round the BASE
     * down and inadvertently include the last bytes of .mpu_diag in
     * the protected region — which would fault the very write the
     * handler tries to make to record the violation. */
    .mpu_diag (NOLOAD) : ALIGN(32)
    {
        __mpu_diag_start = .;
        *(.mpu_diag*)
        . = ALIGN(32);
        __mpu_diag_end = .;
    } > SRAM

    /* "Persistent across reboot but skipped by zero-init" region.
     * The kernel uses this for /sys/boot/count etc. on platforms that
     * have FRAM. On RP2350 the contents survive only warm resets; a
     * power cycle reseeds them as zero. The MPU protects this exact
     * range — see tiku_mpu_arch.c. ALIGN(32) so the MPU region BASE
     * snaps cleanly without rounding into .mpu_diag. */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        *(.uninit*)
        *(.persistent*)
        . = ALIGN(32);
        __uninit_end = .;
    } > SRAM

    /* End of static allocations (heap-style growth would start here).
     * Provide a few aliases for whichever name the linked code uses:
     *   _end          — used by tikuOS / tiku_vfs_tree.c
     *   end           — used by newlib's libnosys _sbrk()
     *   __end__       — used by some libgloss variants
     *   __datastart   — used by tiku_shell_cmd_free.c
     */
    _end = .;
    end = .;
    __end__ = .;
    __datastart = __data_start;

    /* Sanity check: stack must not collide with .bss + .uninit. We
     * leave at least 4 KB headroom between them. */
    ASSERT((__stack - _end) >= 0x1000,
           "ERROR: SRAM exhausted — less than 4 KB free for stack")

    /* Sanity check: .uninit (the SRAM region that the NVM backup
     * sector mirrors) must fit in one flash erase granule. Growing
     * past 4 KB will need multi-sector backup logic in the arch
     * driver. */
    ASSERT((__uninit_end - __uninit_start) <= __tiku_nvm_flash_size,
           "ERROR: .uninit region exceeds 4 KB flash backup sector")

    /* The flash image (code + rodata + the .data load copy) must not grow up
     * into the carved NVM region below the backup sector. */
    ASSERT(__data_load + (__data_end - __data_start) <= __tiku_nvmfs_base,
           "ERROR: flash image overruns the carved NVM region")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}
