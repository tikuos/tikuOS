/* R1a: image lives entirely in user SRAM, loaded by the debugger.
 * Measured extents (this board, today): SRAM 0x22000000 RW to at least
 * 0x221C0000.  Image parked at 0x22020000 -- clear of the factory demo's
 * low-SRAM data and its 0x22004100 boot stack -- with the observation
 * words at 0x22100000, far from both. */
ENTRY(reset_handler)
MEMORY { SRAM (rwx) : ORIGIN = 0x22020000, LENGTH = 64K }
SECTIONS {
    .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > SRAM
    .data : { *(.data*) } > SRAM
    .bss  : { *(.bss*) *(COMMON) } > SRAM
    . = ALIGN(8);
    __stack_top = ORIGIN(SRAM) + LENGTH(SRAM);
}
