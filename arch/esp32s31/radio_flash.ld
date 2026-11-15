/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * radio_flash.ld - S31 radio libraries mapped by the factory bootloader.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
ENTRY(tiku_s31_diagnostic_entry)
MEMORY {
    SRAM (rwx) : ORIGIN = 0x2f000000, LENGTH = DEFINED(S31_FIRMWARE) ? 384K : 256K
    DROM (r) : ORIGIN = 0x40000020, LENGTH = 128K - 32
    IROM (rx) : ORIGIN = 0x40020020, LENGTH = DEFINED(S31_BLE) ? 1024K - 32 : 512K - 32
}
SECTIONS
{
    .flash.rodata : {
        KEEP(*(.app_desc))
        *(EXCLUDE_FILE (*tiku_bt.o *tiku_shell_cmd_bt.o) .rodata* .srodata .srodata.*)
        . = ALIGN(4);
        __tiku_cells_start = .; KEEP(*(.tiku_cells)) __tiku_cells_end = .;
    } > DROM
    .flash.text : {
        *(.flash.text)
        *tiku_bt.o(.rodata* .srodata*)
        *tiku_shell_cmd_bt.o(.rodata* .srodata*)
        *libbtdm_common.a:(.text .text.*) *libble_app.a:(.text .text.*)
        *libbtbb.a:(.text .text.*)
        *libcoexist.a:(.text .text.*)
        *libc.a:libc_a-dtoa.o(.text .text.*)
        *libc.a:libc_a-svfprintf.o(.text .text.*)
        *libc.a:libc_a-vfprintf.o(.text .text.*)
        *libgcc.a:*df*.o(.text .text.*)
        *porting_btdm/*.o(.text .text.*)
        *btdm_broker.o(.text .text.*)
        *tikukits/crypto/*.o(.text .text.*)
        *tiku_bt.o(.text .text.*)
        *tiku_ble*.o(.text .text.*)
        *drivers/wifi/esp/sae/*.o(.text .text.*)
        *esp_wpa.o(.text .text.*)
        *esp_crypto.o(.text .text.*)
        *basic/*.o(.text .text.*)
        *kernel/vfs/*.o(.text .text.*)
        *shell/*.o(.text .text.*)
        *applications/*.o(.text .text.*)
        *libphy.a:(.text .text.*) *libnet80211.a:(.text .text.*) *libpp.a:(.text .text.*) *libcore.a:(.text .text.*)
    } > IROM
    .text : ALIGN(64) {
        KEEP(*(.text.entry))
        *(.text .text.* .iram* .critical_iram* .wifi* )
        _etext = .;
    } > SRAM
    .data : ALIGN(4) {
        __datastart = ORIGIN(SRAM);
        __data_start = .;
        *(.data .data.* .sdata .sdata.* .dram*)
        __global_pointer$ = . + 0x800;
    } > SRAM
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > SRAM
    .uninit (NOLOAD) : ALIGN(4096) {
        __uninit_start = .;
        KEEP(*(.persistent.layout))
        . = __uninit_start + (DEFINED(S31_FIRMWARE) ? 256 : 0);
        KEEP(*(.persistent.arch))
        . = __uninit_start + (DEFINED(S31_FIRMWARE) ? 8192 : 0);
        KEEP(*(.persistent .persistent.*))
        . = ALIGN(4096);
        __uninit_end = .;
    } > SRAM
    ASSERT(SIZEOF(.uninit) <= 16368, "S31 durable mirror capacity exceeded")
    .retained (NOLOAD) : ALIGN(16) {
        *(.retained .retained.*)
        __retained_end = .;
        _end = .;
    } > SRAM
    __stack = ORIGIN(SRAM) + LENGTH(SRAM);
    __tier_sram_start = ALIGN(__retained_end, 64);
    __tier_sram_end = __stack - (DEFINED(S31_FIRMWARE) ? 16384 : 4096);
    __tiku_stack_bottom = __tier_sram_end;
    __tiku_stack_guard_start = __tier_sram_end;
    ASSERT(__retained_end <= __stack - (DEFINED(S31_FIRMWARE) ? 16384 : 4096), "S31 diagnostic overlaps stack")
    .forbidden (NOLOAD) : {
        *(.persistent .persistent.* .retained .retained.*)
    } > SRAM
    ASSERT(SIZEOF(.forbidden) == 0, "S31 diagnostic must be storage-free")
    /DISCARD/ : { *(.comment .note .note.* .eh_frame .eh_frame.*) }
}
