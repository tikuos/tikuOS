/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - route ESP32-C61 builds to their device header.
 *
 * The Makefile defines TIKU_DEVICE_ESP32C61 from the MCU name and the board
 * macro from BOARD; another variant adds an #elif here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_DEVICE_SELECT_H_
#define TIKU_ESP32C61_DEVICE_SELECT_H_

#if defined(TIKU_DEVICE_ESP32C61)
#include <arch/esp32c61/devices/tiku_device_esp32c61.h>
#else
#error "No TikuOS ESP32-C61 device selected. Define TIKU_DEVICE_ESP32C61."
#endif

/*---------------------------------------------------------------------------*/
/* BOARD                                                                     */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_BOARD_ESP32C61_DEVKITC)
#include <arch/esp32c61/boards/tiku_board_esp32c61_devkitc.h>
#else
#error "No TikuOS ESP32-C61 board selected. Define TIKU_BOARD_ESP32C61_DEVKITC."
#endif

#endif /* TIKU_ESP32C61_DEVICE_SELECT_H_ */
