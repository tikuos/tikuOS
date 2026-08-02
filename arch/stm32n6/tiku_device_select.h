/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - route STM32N6 builds to their device header.
 *
 * The Makefile defines TIKU_DEVICE_STM32N6 from the MCU name; another variant
 * adds an #elif here and a header under devices/.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_DEVICE_SELECT_H_
#define TIKU_STM32N6_DEVICE_SELECT_H_

#if defined(TIKU_DEVICE_STM32N6)
#include <arch/stm32n6/devices/tiku_device_stm32n6.h>
#else
#error "No TikuOS STM32N6 device selected. Define TIKU_DEVICE_STM32N6."
#endif

/*---------------------------------------------------------------------------*/
/* BOARD                                                                     */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_BOARD_NUCLEO_N657X0Q)
#include <arch/stm32n6/boards/tiku_board_nucleo_n657x0q.h>
#else
#error "No TikuOS STM32N6 board selected. Define TIKU_BOARD_NUCLEO_N657X0Q."
#endif

#endif /* TIKU_STM32N6_DEVICE_SELECT_H_ */
