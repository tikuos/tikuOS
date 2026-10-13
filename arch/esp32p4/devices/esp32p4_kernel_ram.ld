/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * esp32p4_kernel_ram.ld - CPU0 RAM kernel with two disjoint SRAM spans.
 * SPDX-License-Identifier: Apache-2.0
 */
ENTRY(tiku_esp32p4_reset_handler)
MEMORY {
    XIP (rx) : ORIGIN = 0x40100000, LENGTH = 3M
    LOW (rwx) : ORIGIN = 0x4ff00000, LENGTH = 0x2bbd0
    /* LP ROM stack/data starts at 0x5010fa80; the LP core stays in reset. */
    FAULT (rw) : ORIGIN = 0x5010fa40, LENGTH = 64
    HIGH (rw) : ORIGIN = 0x4ff40000, LENGTH = 384K
}
SECTIONS
{
    .xip : {
        . += DEFINED(tiku_p4_xip_expected) ? 64 : 0;
        KEEP(*(.p4.xip.text .p4.xip.rodata))
        . = ALIGN(4);
    } > XIP
    ASSERT(SIZEOF(.xip) == 0 || SIZEOF(.xip) > 64,
           "P4 XIP payload must contain code")
    .text : ALIGN(64) {
        KEEP(*(.text.entry))
        *(.text .text.*)
        . = ALIGN(4);
    } > LOW
    .rodata : ALIGN(4) {
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
    } > LOW
    .p4.expected : ALIGN(4) { KEEP(*(.p4.expected)) } > LOW
    .data : ALIGN(4) {
        *(.data .data.*)
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > LOW
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(16);
        __uninit_start = .;
        KEEP(*(.persistent.layout))
        KEEP(*(.persistent .persistent.*))
        __uninit_end = .;
        . = ALIGN(16);
        __bss_end = .;
    } > LOW
    .fault_record (NOLOAD) : {
        KEEP(*tiku_fault_arch.o(.retained))
    } > FAULT
    ASSERT(SIZEOF(.fault_record) == 64, "P4 fault record must be 64 bytes")
    .forbidden (NOLOAD) : {
        *(.retained .retained.*)
    } > LOW
    __tier_sram_extra_start = __bss_end;
    __tier_sram_extra_end = ORIGIN(LOW) + LENGTH(LOW);
    __stack = ORIGIN(HIGH) + LENGTH(HIGH);
    __stack_bottom = __stack - 4K;
    .stack_guard (__stack_bottom - 64) (NOLOAD) : {
        __stack_guard = .;
        . += 64;
    } > HIGH
    .stack __stack_bottom (NOLOAD) : { . += 4K; } > HIGH
    __tiku_tier_span_start = ORIGIN(HIGH);
    __tiku_tier_span_limit = __stack_guard;
    __tiku_tier_reserve_default = 0;
    __tiku_tier_floor_default = 64K;
    INCLUDE arch/common/tiku_sram_layout.ld
    ASSERT(__tier_sram_extra_start <= __tier_sram_extra_end,
           "P4 low SRAM statics overlap ROM workspace")
    ASSERT(__tier_sram_extra_end - __tier_sram_extra_start >= 16K,
           "P4 low SRAM allocator span below 16 KiB")
    ASSERT(SIZEOF(.forbidden) == 0, "P4 RAM kernel must be storage-free")
    ASSERT(DEFINED(__p4_storage_enabled) || __uninit_start == __uninit_end,
           "P4 durable objects require the storage profile")
    ASSERT(__uninit_end - __uninit_start <= 16K - 16,
           "P4 durable image exceeds the mirror payload")
    /DISCARD/ : { *(.comment .note .note.*) }
}
