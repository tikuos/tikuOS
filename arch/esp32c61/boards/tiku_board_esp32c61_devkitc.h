/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_esp32c61_devkitc.h - Espressif ESP32-C61-DevKitC.
 *
 * What the PCB adds to the part: an addressable RGB LED, the console bridge,
 * the BOOT button, and which USB port carries what.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_ESP32C61_DEVKITC_H_
#define TIKU_BOARD_ESP32C61_DEVKITC_H_

#include <arch/esp32c61/tiku_gpio_arch.h>

#define TIKU_BOARD_NAME             "ESP32-C61-DevKitC"

/* One addressable RGB LED (WS2812-class, GRB order) on GPIO8. */
#define TIKU_BOARD_RGB_LED_GPIO     8U

/* The kernel's three LEDs are that LED's red, green and blue channels
 * (tiku_esp32c61_led_set() channels 0, 1 and 2), each switched alone. */
#define TIKU_BOARD_LED_COUNT        3
#define TIKU_BOARD_LED_PIN          TIKU_BOARD_RGB_LED_GPIO
#define TIKU_BOARD_LED1_INIT()      tiku_esp32c61_gpio_init_output(TIKU_BOARD_LED_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_esp32c61_led_set(TIKU_BOARD_LED_PIN, 0U, 1)
#define TIKU_BOARD_LED1_OFF()       tiku_esp32c61_led_set(TIKU_BOARD_LED_PIN, 0U, 0)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_esp32c61_led_set(TIKU_BOARD_LED_PIN, 0U, -1)
#define TIKU_BOARD_LED2_INIT()      tiku_esp32c61_gpio_init_output(TIKU_BOARD_LED_PIN)
#define TIKU_BOARD_LED2_ON()        tiku_esp32c61_led_set(TIKU_BOARD_LED_PIN, 1U, 1)
#define TIKU_BOARD_LED2_OFF()       tiku_esp32c61_led_set(TIKU_BOARD_LED_PIN, 1U, 0)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_esp32c61_led_set(TIKU_BOARD_LED_PIN, 1U, -1)
#define TIKU_BOARD_LED3_INIT()      tiku_esp32c61_gpio_init_output(TIKU_BOARD_LED_PIN)
#define TIKU_BOARD_LED3_ON()        tiku_esp32c61_led_set(TIKU_BOARD_LED_PIN, 2U, 1)
#define TIKU_BOARD_LED3_OFF()       tiku_esp32c61_led_set(TIKU_BOARD_LED_PIN, 2U, 0)
#define TIKU_BOARD_LED3_TOGGLE()    tiku_esp32c61_led_set(TIKU_BOARD_LED_PIN, 2U, -1)

/* UART0 (GPIO11 TX, GPIO10 RX) reaches the host through the CP2102N; the
 * chip's own USB-Serial/JTAG is the other connector and is what esptool
 * loads through. Opening the CP2102N port resets the chip. */
#define TIKU_BOARD_CONSOLE_UART     0U
#define TIKU_BOARD_UART0_TX_GPIO    11U
#define TIKU_BOARD_UART0_RX_GPIO    10U

/* BOOT button: GPIO9 (port 2 pin 1), low when pressed; held at reset it
 * enters the ROM's download mode. */
#define TIKU_BOARD_BOOT_BUTTON_GPIO 9U
#define TIKU_BOARD_BTN1_PORT        2U
#define TIKU_BOARD_BTN1_PIN         1U
#define TIKU_BOARD_BTN1_INIT()      (void)tiku_gpio_arch_set_input(TIKU_BOARD_BTN1_PORT, TIKU_BOARD_BTN1_PIN)
#define TIKU_BOARD_BTN1_PRESSED()   (tiku_gpio_arch_read(TIKU_BOARD_BTN1_PORT, TIKU_BOARD_BTN1_PIN) == 0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/* ADC, I2C, 1-Wire and SPI have no driver on this port, so their pins are
 * placeholders.  No backscatter pin is defined: the in-package PSRAM shares
 * some of the header pins. */
#define TIKU_BOARD_ADC_AVAILABLE    0
#define TIKU_BOARD_I2C_BRW_100K     1   /* symbolic */
#define TIKU_BOARD_OW_AVAILABLE     0
#define TIKU_BOARD_OW_PIN           0U
#define TIKU_BOARD_I2C0_SDA_PIN     0U
#define TIKU_BOARD_I2C0_SCL_PIN     0U
#define TIKU_BOARD_SPI0_MISO_PIN    0U
#define TIKU_BOARD_SPI0_SCK_PIN     0U
#define TIKU_BOARD_SPI0_MOSI_PIN    0U

#endif /* TIKU_BOARD_ESP32C61_DEVKITC_H_ */
