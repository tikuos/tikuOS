/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_firebeetle2_esp32p4.h - FireBeetle 2 LED, button and header pins.
 *
 * Pin roles follow the DFR1172 schematic V1.0.  TIKU_BOARD_GPIO_RESERVED
 * holds the pads wired to the ESP32-C6, the TF slot, the microphone, the
 * USB console, the LED and the BOOT button; the GPIO API refuses them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_BOARD_FIREBEETLE2_ESP32P4_H_
#define TIKU_BOARD_FIREBEETLE2_ESP32P4_H_

#define TIKU_BOARD_NAME "DFRobot FireBeetle 2 ESP32-P4"

/* Type-C "CDC" port: the USB Serial/JTAG PHY 0 pads. */
#define TIKU_BOARD_USB_DM_PIN 24u
#define TIKU_BOARD_USB_DP_PIN 25u

/* Header P1 pins 17/18, UART0's IO_MUX pads; the ROM prints its log there. */
#define TIKU_BOARD_UART_TX_PIN 37u
#define TIKU_BOARD_UART_RX_PIN 38u
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD 115200u
#endif

/* GPIO3 drives the green user LED through 2.2 kOhm to ground: high is lit.
 * GPIO35, the BOOT button, shorts to ground; its external pull-up is not
 * fitted, so the button driver turns the pad's pull-up on. */
#define TIKU_BOARD_LED_PIN 3u
#define TIKU_BOARD_BTN_PIN 35u
#include <arch/esp32p4/tiku_gpio_arch.h>
#define TIKU_BOARD_LED_COUNT     1
#define TIKU_BOARD_LED1_INIT()   tiku_p4_led_init()
#define TIKU_BOARD_LED1_ON()     tiku_p4_led_set(1)
#define TIKU_BOARD_LED1_OFF()    tiku_p4_led_set(0)
#define TIKU_BOARD_LED1_TOGGLE() tiku_p4_led_set(-1)
#define TIKU_BOARD_BTN_COUNT       1
#define TIKU_BOARD_BTN1_INIT()     tiku_p4_button_init()
#define TIKU_BOARD_BTN1_PRESSED()  tiku_p4_button_pressed()

/* 6, 14-19, 54: the ESP32-C6's SDIO link, EN and WAKEUP.  9, 12: the PDM
 * microphone.  24, 25: the console.  39-45: the TF slot and its power switch
 * (GPIO45 low powers the card).  3, 35: the LED and the button. */
#define TIKU_BOARD_GPIO_RESERVED                                             \
    ((1ULL << 3) | (1ULL << 6) | (1ULL << 9) | (1ULL << 12) |               \
     (0x3FULL << 14) | (3ULL << 24) | (1ULL << 35) | (0x7FULL << 39) |      \
     (1ULL << 54))

#endif
