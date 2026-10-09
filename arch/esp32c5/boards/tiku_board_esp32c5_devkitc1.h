/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_board_esp32c5_devkitc1.h - DevKitC-1 console and RGB LED pins.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_BOARD_ESP32C5_DEVKITC1_H_
#define TIKU_BOARD_ESP32C5_DEVKITC1_H_

#define TIKU_BOARD_NAME              "ESP32-C5-DevKitC-1"
#define TIKU_BOARD_USB_DM_PIN         13u
#define TIKU_BOARD_USB_DP_PIN         14u
#define TIKU_BOARD_UART_TX_PIN        11u
#define TIKU_BOARD_UART_RX_PIN        12u
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD          115200u
#endif
#define TIKU_BOARD_RGB_LED_PIN        27u
/* I2C is routed only by an explicit tiku_i2c_init() call. External pull-ups
 * to 3.3 V are required; the internal pulls are not enabled by the driver. */
#ifndef TIKU_BOARD_I2C_SDA_PIN
#define TIKU_BOARD_I2C_SDA_PIN        8u
#endif
#ifndef TIKU_BOARD_I2C_SCL_PIN
#define TIKU_BOARD_I2C_SCL_PIN        9u
#endif
/* The portable bus uses this macro as its board capability test. */
#define TIKU_BOARD_I2C_BRW_100K       480u
/* The C5 IO-mux reference lists straps on 2/3/7/25..28. GPIO27 also drives
 * the addressable LED; 13/14 carry USB and 15..22 carry flash/PSRAM. */
#define TIKU_BOARD_GPIO_RESERVED ((3UL << 2) | (1UL << 7) | (15UL << 25) | \
                                  (3UL << 13) | (255UL << 15))

#endif
