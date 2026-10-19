/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_esp32c5_devkitc1.h - DevKitC-1 console, RGB LED and bus pins.
 *
 * The RGB LED's red, green and blue channels are the kernel's three LEDs.
 * Bus pins can be overridden at build time; TIKU_BOARD_GPIO_RESERVED holds
 * the strap, USB, flash/PSRAM and LED pads the GPIO API refuses.
 *
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
/* GP-SPI2 through the GPIO matrix; chip select is the caller's GPIO. */
#ifndef TIKU_BOARD_SPI_SCK_PIN
#define TIKU_BOARD_SPI_SCK_PIN 10u
#endif
#ifndef TIKU_BOARD_SPI_MOSI_PIN
#define TIKU_BOARD_SPI_MOSI_PIN 23u
#endif
#ifndef TIKU_BOARD_SPI_MISO_PIN
#define TIKU_BOARD_SPI_MISO_PIN 24u
#endif
/* SAR ADC1 channels 0..5 on GPIO1..6; GPIO2..5 are straps and JTAG, so
 * channels 1..4 open only once their pad is a GPIO. */
#define TIKU_BOARD_ADC_AVAILABLE 1
/* 1-Wire, open drain; devices need an external 4.7 kOhm pull-up.  GPIO6 is
 * ADC channel 5's pad too; GPIO0 and GPIO1, the 32 kHz crystal's pads, do
 * not hold the internal pull-up on this board. */
#define TIKU_BOARD_OW_AVAILABLE 1
#ifndef TIKU_BOARD_OW_PIN
#define TIKU_BOARD_OW_PIN 6u
#endif
/* The C5 IO-mux reference lists straps on 2/3/7/25..28. GPIO27 also drives
 * the addressable LED; 13/14 carry USB and 15..22 carry flash/PSRAM. */
#define TIKU_BOARD_GPIO_RESERVED                                               \
    ((3UL << 2) | (1UL << 7) | (15UL << 25) | (3UL << 13) | (255UL << 15))

#endif
