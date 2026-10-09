/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * xip_code.ld - C5 application code and constants mapped from flash.
 * Kernel and architecture code stay in SRAM. Vendor .iram1 stays in SRAM.
 * SPDX-License-Identifier: Apache-2.0
 */
.xip : ALIGN(32) {
    __tiku_c5_xip_start = .;
    BYTE(0);
    . += 63;
    @TIKU_C5_OBJECT_ROOT@/basic/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    @TIKU_C5_OBJECT_ROOT@/shell/commands/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    @TIKU_C5_OBJECT_ROOT@/shell/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    @TIKU_C5_OBJECT_ROOT@/drivers/wifi/esp/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    KEEP(*(.xip.roots))
    *libphy.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    *libnet80211.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .rodata_wlog* .wifi0iram .wifi0iram.* .wifirxiram .wifirxiram.* .wifiextrairam .wifiextrairam.* .wifislprxiram .wifislprxiram.* .wifislpiram .wifislpiram.*)
    *libpp.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .rodata_wlog* .wifi0iram .wifi0iram.* .wifirxiram .wifirxiram.* .wifiextrairam .wifiextrairam.* .wifislprxiram .wifislprxiram.* .wifislpiram .wifislpiram.*)
    *libcore.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .rodata_wlog*)
    *libble_app.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .iram1 .iram1.* .adv_fast_execute_code_iram1 .adv_fast_execute_code_iram1.* .iram_sleep .iram_sleep.* .iram_btslp .iram_btslp.* .iram_sleep_adv .iram_sleep_adv.* .iram_sleep_conn .iram_sleep_conn.* .iram_single_sleep .iram_single_sleep.*)
    *libbtbb.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .iram1 .iram1.*)
    @TIKU_C5_OBJECT_ROOT@/tikukits/net/bluetooth/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    @TIKU_C5_OBJECT_ROOT@/tikukits/crypto/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    . = ALIGN(4);
    __tiku_c5_xip_end = .;
} > XIP
__tiku_c5_xip_bytes = SIZEOF(.xip);
/DISCARD/ : { *libphy.a:*(.eh_frame .eh_frame.*) *libble_app.a:*(.eh_frame .eh_frame.*) *libbtbb.a:*(.eh_frame .eh_frame.*) }
