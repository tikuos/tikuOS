/*
 * xip_code.ld - kernel code that runs from flash (ESP32-C61), with
 * TIKU_ESP32C61_XIP_CODE=1: BASIC, the shell's commands, the IP stack above
 * SLIP, TLS and the crypto under it, the BLE host, and the /sys, /sys/boot
 * and Wi-Fi nodes -- code and constants; data stays put.
 *
 * None of it runs in an interrupt or while a flash write has the cache
 * suspended, and the flash driver stages a write whose source is a constant
 * here through SRAM.  SLIP stays in SRAM: its bytes may come from the UART.
 * The nodes are here to keep SRAM bank 3 out of the kernel's image: the
 * SDR receiver captures into all 64 KB of it.
 */

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
