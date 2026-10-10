/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_board_esp32c5_devkitc1.h - DevKitC-1 console and RGB LED pins.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_BOARD_ESP32C5_DEVKITC1_H_
#define TIKU_BOARD_ESP32C5_DEVKITC1_H_

#define TIKU_BOARD_NAME        "ESP32-C5-DevKitC-1"
#define TIKU_BOARD_USB_DM_PIN  13u
#define TIKU_BOARD_USB_DP_PIN  14u
#define TIKU_BOARD_UART_TX_PIN 11u
#define TIKU_BOARD_UART_RX_PIN 12u
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD 115200u
#endif
#define TIKU_BOARD_RGB_LED_PIN 27u

#include <arch/esp32c5/tiku_gpio_arch.h>

/* The kernel's three LEDs are the RGB LED's red, green and blue channels
 * (tiku_c5_led_set() channels 0, 1 and 2), each switched alone. */
#define TIKU_BOARD_LED_COUNT     3
#define TIKU_BOARD_LED1_INIT()   tiku_c5_led_init()
#define TIKU_BOARD_LED1_ON()     tiku_c5_led_set(0u, 1)
#define TIKU_BOARD_LED1_OFF()    tiku_c5_led_set(0u, 0)
#define TIKU_BOARD_LED1_TOGGLE() tiku_c5_led_set(0u, -1)
#define TIKU_BOARD_LED2_INIT()   tiku_c5_led_init()
#define TIKU_BOARD_LED2_ON()     tiku_c5_led_set(1u, 1)
#define TIKU_BOARD_LED2_OFF()    tiku_c5_led_set(1u, 0)
#define TIKU_BOARD_LED2_TOGGLE() tiku_c5_led_set(1u, -1)
#define TIKU_BOARD_LED3_INIT()   tiku_c5_led_init()
#define TIKU_BOARD_LED3_ON()     tiku_c5_led_set(2u, 1)
#define TIKU_BOARD_LED3_OFF()    tiku_c5_led_set(2u, 0)
#define TIKU_BOARD_LED3_TOGGLE() tiku_c5_led_set(2u, -1)
/* I2C is routed only by an explicit tiku_i2c_init() call. External pull-ups
 * to 3.3 V are required; the internal pulls are not enabled by the driver. */
#ifndef TIKU_BOARD_I2C_SDA_PIN
#define TIKU_BOARD_I2C_SDA_PIN 8u
#endif
#ifndef TIKU_BOARD_I2C_SCL_PIN
#define TIKU_BOARD_I2C_SCL_PIN 9u
#endif
/* The portable bus uses this macro as its board capability test. */
#define TIKU_BOARD_I2C_BRW_100K 480u
/* The C5 IO-mux reference lists straps on 2/3/7/25..28. GPIO27 also drives
 * the addressable LED; 13/14 carry USB and 15..22 carry flash/PSRAM. */
#define TIKU_BOARD_GPIO_RESERVED                                               \
    ((3UL << 2) | (1UL << 7) | (15UL << 25) | (3UL << 13) | (255UL << 15))

#endif
