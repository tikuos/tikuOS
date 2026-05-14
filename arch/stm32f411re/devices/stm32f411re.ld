/*
 * Tiku Operating System — STM32F411RE (Nucleo-F411RE) linker script
 *
 * Memory map (STM32F411RE Datasheet §5):
 *   FLASH : 512 KB internal at 0x08000000
 *   SRAM  : 128 KB at 0x20000000
 *
 * Layout in flash:
 *   0x08000000  .vectors   (M4 vector table; SP and reset handler)
 *   0x08000xxx  .text      (kernel + app code)
 *   0x08xxxxx   .rodata    (read-only data)
 *   [LMA]       .data      (load address; copied to SRAM at boot)
 *
 * Layout in SRAM:
 *   .data       (initialised variables, copied from flash at boot)
 *   .bss        (zeroed at boot)
 *   .mpu_diag   (NOLOAD, 32-byte aligned — fault state across warm reset)
 *   .uninit     (NOLOAD, 32-byte aligned — persistent warm-reset state)
 *   __stack     (top of SRAM, grows downward)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(tiku_stm32f411_reset_handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

__sram_start  = ORIGIN(SRAM);
__sram_end    = ORIGIN(SRAM) + LENGTH(SRAM);
__flash_start = ORIGIN(FLASH);
__flash_end   = ORIGIN(FLASH) + LENGTH(FLASH);

/* The Cortex-M loads the initial SP from the first vector entry.
 * Place it at the very top of SRAM and let the stack grow downward. */
__stack = __sram_end;

/*
 * No NVM backup sector on STM32F411RE internal flash.
 * NVM symbols set to 0 to mitigate errors.
 */
__tiku_nvm_flash_size   = 0;
__tiku_nvm_flash_offset = 0;
__tiku_nvm_flash_start  = 0;
__tiku_nvm_flash_end    = 0;

SECTIONS
{
    /* -------- 1. Cortex-M4 vector table ------------------------------ */
    .vectors ORIGIN(FLASH) :
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        __vectors_end = .;
    } > FLASH

    /* -------- 2. Code & read-only data ------------------------------- */
    .text : ALIGN(4)
    {
        *(.text*)
        *(.glue_7) *(.glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > FLASH

    .rodata : ALIGN(4)
    {
        *(.rodata*)
        *(.gnu.linkonce.r.*)
    } > FLASH

    .ARM.extab : { *(.ARM.extab*) } > FLASH
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH

    /* -------- 3. Initialised data ------------------------------------ */
    __data_load = LOADADDR(.data);
    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM AT > FLASH

    /* -------- 4. Zero-initialised BSS -------------------------------- */
    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SRAM

    /* -------- 5. MPU diagnostic state (NOLOAD) ----------------------- */
    .mpu_diag (NOLOAD) : ALIGN(32)
    {
        __mpu_diag_start = .;
        *(.mpu_diag*)
        . = ALIGN(32);
        __mpu_diag_end = .;
    } > SRAM

    /* -------- 6. Persistent warm-reset state (NOLOAD) ---------------- */
    .uninit (NOLOAD) : ALIGN(32)
    {
        __uninit_start = .;
        *(.uninit*)
        *(.persistent*)
        . = ALIGN(32);
        __uninit_end = .;
    } > SRAM

    /* Heap start aliases */
    _end       = .;
    end        = .;
    __end__    = .;
    __datastart = __data_start;

    /* Sanity checks */
    ASSERT((__stack - _end) >= 0x1000,
           "ERROR: SRAM exhausted — less than 4 KB free for stack")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.ARM.attributes)
    }
}