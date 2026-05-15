/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_device_select.h - STM32F411RE device + board include router
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_DEVICE_SELECT_H_
#define TIKU_STM32F411_DEVICE_SELECT_H_

#if defined(TIKU_DEVICE_STM32F411RE)
#include <arch/stm32f411re/devices/tiku_device_stm32f411re.h>
#else
#error "No TikuOS STM32F411 device selected. Define TIKU_DEVICE_STM32F411RE."
#endif

#if defined(TIKU_BOARD_NUCLEO_F411RE)
#include <arch/stm32f411re/boards/tiku_board_nucleo_f411re.h>
#else
#define TIKU_BOARD_NUCLEO_F411RE 1
#include <arch/stm32f411re/boards/tiku_board_nucleo_f411re.h>
#endif

#endif /* TIKU_STM32F411_DEVICE_SELECT_H_ */
