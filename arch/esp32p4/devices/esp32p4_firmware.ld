/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * esp32p4_firmware.ld - CPU0 firmware with paired flash application code.
 *
 * Shell, BASIC, applications, kits, tests, the VFS tree and the LP core image
 * link to flash at 0x40100000; the rest to low HP SRAM, the durable image last
 * in .bss.  High SRAM holds the tier span and the 16 KiB stack.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
ENTRY(tiku_esp32p4_reset_handler)
MEMORY {
    XIP (rx) : ORIGIN = 0x40100000, LENGTH = 3M
    LOW (rwx) : ORIGIN = 0x4ff00000, LENGTH = 0x2bbd0
    FAULT (rw) : ORIGIN = 0x5010fa40, LENGTH = 64
    HIGH (rw) : ORIGIN = 0x4ff40000, LENGTH = 384K
}
SECTIONS {
    .xip : {
        . += 64;
        */basic/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        */shell/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        */applications/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        */TikuBench/tests/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        */kernel/vfs/tree/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        */drivers/wifi/ipc/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        */drivers/ble/ipc/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        */interfaces/bluetooth/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        */services/ipc/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_ipc_stream.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_sdio_owner_arch.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_sdmmc_arch.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_sdcard_arch.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_media_probe.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_fat.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_sd_fat.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_sd_vfs.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_mfs.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_mfs_copy.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_mfs_block_arch.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_media_card.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_media_store.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_media_vfs.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_link_uart.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_uart_port_arch.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_boot_ipc.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *tiku_boot_c6.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        */interfaces/coproc/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        */tikukits/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
        *(.p4.xip.text .p4.xip.rodata)
        *(.rodata.tiku_lpcore_image)
        . = ALIGN(4);
    } > XIP
    .text : ALIGN(64) {
        KEEP(*(.text.entry))
        *(.text .text.*)
        . = ALIGN(4);
    } > LOW
    .rodata : ALIGN(4) {
        KEEP(*(.p4.firmware))
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
        __tiku_cells_start = .;
        KEEP(*(.tiku_cells))
        __tiku_cells_end = .;
        . = ALIGN(4);
    } > LOW
    _etext = .;
    .p4.expected : ALIGN(4) { KEEP(*(.p4.expected)) } > LOW
    .data : ALIGN(4) {
        *(.data .data.*)
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > LOW
    __datastart = ORIGIN(LOW);
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        EXCLUDE_FILE (*/tikukits/net/*.o *tiku_drv_wifi_ipc.o *tiku_drv_ble_ipc.o *tiku_ble_serial.o *tiku_ble_adv_hci.o *test_radio.o *tiku_media_card.o) *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(16);
        __uninit_start = .;
        KEEP(*(.persistent.layout))
        /* Append Wi-Fi settings without moving existing durable records. */
        KEEP(*(EXCLUDE_FILE (*tiku_drv_wifi_ipc.o) .persistent .persistent.*))
        KEEP(*tiku_drv_wifi_ipc.o(.persistent .persistent.*))
        __uninit_end = .;
        . = ALIGN(16);
        __bss_end = .;
    } > LOW
    .fault_record (NOLOAD) : { KEEP(*tiku_fault_arch.o(.retained)) } > FAULT
    .retained (NOLOAD) : ALIGN(16) {
        __retained_start = .;
        *(.retained .retained.*)
        . = ALIGN(16);
        __retained_end = .;
    } > LOW
    _end = .;
    __tiku_stack_bottom = __stack_bottom;
    __tiku_stack_guard_start = __stack_guard;
    __tiku_layout_code_cap = 0x40400000;
    __tiku_code_limit = __tiku_layout_code_cap;
    __tiku_layout_persist_size = 32K;
    __tiku_layout_scratch_size = 4K;
    __tiku_layout_reserved_tail_size = 28K;
    __tiku_flash_payload_bytes = SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data)
                                + SIZEOF(.p4.expected) + SIZEOF(.xip);
    __tier_sram_extra_start = __retained_end;
    __tier_sram_extra_end = ORIGIN(LOW) + LENGTH(LOW);
    __stack = ORIGIN(HIGH) + LENGTH(HIGH);
    __stack_bottom = __stack - 16K;
    .ipc_device ORIGIN(HIGH) (NOLOAD) : ALIGN(64) {
        __ipc_device_start = .;
        __sdmmc_descriptor_start = .;
        *(.p4.ipc.sdmmc_descriptor)
        __sdmmc_descriptor_end = .;
        *(.p4.ipc .p4.ipc.*)
        *tiku_drv_wifi_ipc.o(.bss .bss.* .sbss .sbss.*)
        */tikukits/net/*.o(.bss .bss.* .sbss .sbss.*)
        *tiku_drv_ble_ipc.o(.bss .bss.* .sbss .sbss.*)
        *tiku_ble_serial.o(.bss .bss.* .sbss .sbss.*)
        *tiku_ble_adv_hci.o(.bss .bss.* .sbss .sbss.*)
        *test_radio.o(.bss .bss.* .sbss .sbss.*)
        *tiku_media_card.o(.bss .bss.* .sbss .sbss.*)
        . = ALIGN(64);
        __ipc_device_end = .;
    } > HIGH
    .stack_guard (__stack_bottom - 64) (NOLOAD) : {
        __stack_guard = .;
        . += 64;
    } > HIGH
    .stack __stack_bottom (NOLOAD) : { . += 16K; } > HIGH
    __tiku_tier_span_start = __ipc_device_end;
    __tiku_tier_span_limit = __stack_guard;
    __tiku_tier_reserve_default = 0;
    __tiku_tier_floor_default = 256K;
    INCLUDE arch/common/tiku_sram_layout.ld
    ASSERT(__tier_sram_extra_start <= __tier_sram_extra_end,
           "P4 low SRAM statics overlap ROM workspace")
    ASSERT(SIZEOF(.ipc_device) <= 64K, "P4 device staging exceeds firmware validator budget")
    ASSERT(SIZEOF(.fault_record) == 64, "P4 fault record must be 64 bytes")
    ASSERT(__uninit_start == tiku_layout_pin, "P4 layout record must open mirror")
    ASSERT(__uninit_end - __uninit_start <= 16K - 16,
           "P4 durable image exceeds mirror payload")
    /DISCARD/ : { *(.comment .note .note.*) }
}
