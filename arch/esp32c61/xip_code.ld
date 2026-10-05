/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * xip_code.ld - ESP32-C61 kernel code that runs from flash.
 *
 * With TIKU_ESP32C61_XIP_CODE=1: code and constants of BASIC, shell commands,
 * the crypto, IPv4, HTTP, MQTT, TLS, WiFi and Bluetooth kits, and the /sys,
 * /sys/boot and Wi-Fi VFS nodes.  Their data stays in SRAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* None of this code runs in an interrupt or while a flash write has the
 * cache suspended, and the flash driver copies a write's source from here
 * to SRAM before programming it.  The SLIP kit stays in SRAM: its bytes may
 * come from the UART.  The VFS nodes are here to keep SRAM bank 3 out of the
 * kernel's image: the SDR receiver captures into all 64 KB of it. */

.xip.code :
{
    __tiku_xip_code_start = .;
    *kernel/shell/basic/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    *kernel/shell/commands/*.o(.text .text.* .rodata .rodata.*
                               .srodata .srodata.*)
    *tikukits/crypto/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    *tikukits/net/ipv4/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    *tikukits/net/http/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    *tikukits/net/mqtt/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    *tikukits/net/tls/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    *tikukits/net/wifi/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    *tikukits/net/bluetooth/*.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
    *kernel/vfs/tree/tiku_vfs_tree_sys.o(.text .text.* .rodata .rodata.*
                                         .srodata .srodata.*)
    *kernel/vfs/tree/tiku_vfs_tree_boot.o(.text .text.* .rodata .rodata.*
                                          .srodata .srodata.*)
    *kernel/vfs/tree/tiku_vfs_tree_wifi.o(.text .text.* .rodata .rodata.*
                                          .srodata .srodata.*)
    . = ALIGN(4);
    __tiku_xip_code_end = .;
} > XIP
