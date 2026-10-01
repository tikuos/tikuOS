/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_esp32c61.h - ESP32-C61 silicon facts.
 *
 * One RV32IMAC core, 320 KB of HP SRAM that holds the whole image, flash
 * behind the MMU as storage, PSRAM in the package.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_ESP32C61_H_
#define TIKU_DEVICE_ESP32C61_H_

#define TIKU_DEVICE_NAME            "ESP32-C61"

/* One GPIO bank of 30 pins, GPIO0..GPIO29, which the kernel sees as four
 * 1-based ports of eight, as on the RP2350; port 4 stops at pin 5. */
#define TIKU_DEVICE_GPIO_COUNT      30
#define TIKU_DEVICE_HAS_PORT1       1
#define TIKU_DEVICE_HAS_PORT2       1
#define TIKU_DEVICE_HAS_PORT3       1
#define TIKU_DEVICE_HAS_PORT4       1
#define TIKU_DEVICE_HAS_PORT5       0
#define TIKU_DEVICE_HAS_PORT6       0
#define TIKU_DEVICE_HAS_PORT7       0
#define TIKU_DEVICE_HAS_PORT8       0
#define TIKU_DEVICE_HAS_PORT9       0
#define TIKU_DEVICE_HAS_PORTJ       0

/* The 40 MHz crystal is the reference for everything: the PLL behind the
 * 160 MHz core, the 16 MHz SYSTIMER, UART baud rates. */
#define TIKU_DEVICE_HAS_LFXT        0
#define TIKU_DEVICE_HAS_HFXT        1
#define TIKU_DEVICE_XOSC_HZ         40000000UL
#define TIKU_DEVICE_CS_HAS_KEY      0
#define TIKU_DEVICE_MAX_STABLE_MHZ  160

/* HP SRAM, unified I/D. The image is linked into the low 304 KB; the top
 * 16 KB holds the ROM's stack and data, which its delay and flash calls
 * still need, so nothing here claims it. */
#define TIKU_DEVICE_RAM_START       0x40800000UL
#define TIKU_DEVICE_RAM_SIZE        0x50000UL
#define TIKU_DEVICE_IMAGE_WINDOW_START  0x40800000UL
#define TIKU_DEVICE_IMAGE_WINDOW_SIZE   0x4C000UL

/* No internal NVM: the durable store will be the 8 MB XMC flash, described
 * by the window the MMU can map it into.  Code runs from SRAM, so the in-use
 * figure derived from _etext correctly comes out zero. */
#define TIKU_DEVICE_FRAM_SIZE       0x00800000UL
#define TIKU_DEVICE_FRAM_START      0x42000000UL
#define TIKU_DEVICE_FRAM_END        0x427FFFFFUL
#define TIKU_DEVICE_NVM_LABEL       "Flash"

/* The init table's durable region: a 4-byte header and 8 entries of 66
 * bytes is 532, rounded up to 64-byte alignment as on the other parts. */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE    576U

/* The Makefile passes UART_BAUD on the command line; this is the default. */
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        115200U
#endif

#endif /* TIKU_DEVICE_ESP32C61_H_ */
