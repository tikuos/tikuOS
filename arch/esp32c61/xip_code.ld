/*
 * xip_code.ld - kernel code that runs from flash (ESP32-C61), with
 * TIKU_ESP32C61_XIP_CODE=1: BASIC, the shell's commands, the IP stack above
 * SLIP, TLS and the crypto under it -- code and constants; data stays put.
 *
 * None of it runs in an interrupt or while a flash write has the cache
 * suspended, and the flash driver stages a write whose source is a constant
 * here through SRAM.  SLIP stays in SRAM: its bytes may come from the UART.
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
    . = ALIGN(4);
    __tiku_xip_code_end = .;
} > XIP
