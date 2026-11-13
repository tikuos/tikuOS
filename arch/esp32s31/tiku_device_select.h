/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - ESP32-S31 Korvo identity and SRAM configuration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32S31_DEVICE_SELECT_H_
#define TIKU_ESP32S31_DEVICE_SELECT_H_
#if !defined(TIKU_BOARD_ESP32S31_KORVO1)
#error "ESP32-S31 requires the Korvo-1 board selection"
#endif
#define TIKU_DEVICE_NAME "ESP32-S31 rev 0.0"
#define TIKU_BOARD_NAME "ESP32-S31-Korvo-1"
#define TIKU_DEVICE_RAM_START 0x2f000000UL
#define TIKU_DEVICE_RAM_SIZE (512UL * 1024UL)
#define TIKU_DEVICE_RAM_USABLE (64UL * 1024UL)
#define TIKU_DEVICE_FRAM_SIZE 0UL
#define TIKU_DEVICE_NVM_LABEL "NVM"
#define TIKU_DEVICE_XOSC_HZ 40000000UL
#define TIKU_BOARD_UART_TX_PIN 58u
#define TIKU_BOARD_UART_RX_PIN 59u
#define TIKU_BOARD_I2C_SDA_PIN 0u
#define TIKU_BOARD_I2C_SCL_PIN 1u
#define TIKU_BOARD_I2C_BRW_100K 400u
#define TIKU_BOARD_LED_COUNT 0
#define TIKU_BOARD_BTN_COUNT 0
#endif
