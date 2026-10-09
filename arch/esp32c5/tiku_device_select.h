/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_device_select.h - require the ESP32-C5 device and board selection.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_DEVICE_SELECT_H_
#define TIKU_ESP32C5_DEVICE_SELECT_H_

#if !defined(PLATFORM_ESP32C5) || !defined(TIKU_DEVICE_ESP32C5)
#error "ESP32-C5 sources require PLATFORM_ESP32C5 and TIKU_DEVICE_ESP32C5"
#endif
#if defined(PLATFORM_ESP32C61) || defined(TIKU_DEVICE_ESP32C61)
#error "ESP32-C5 and ESP32-C61 definitions cannot be combined"
#endif
#if !defined(TIKU_BOARD_ESP32C5_DEVKITC1)
#error "Select TIKU_BOARD_ESP32C5_DEVKITC1"
#endif

#include "devices/tiku_device_esp32c5.h"
#include "boards/tiku_board_esp32c5_devkitc1.h"

#endif
