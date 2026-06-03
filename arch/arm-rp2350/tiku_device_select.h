/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - RP2350 device + board include router
 *
 * Mirrors arch/msp430/tiku_device_select.h. The Makefile sets one of:
 *   TIKU_DEVICE_RP2350      (silicon)
 *   TIKU_BOARD_RPI_PICO2_W  (board PCB definitions)
 *
 * Adding a new RP2350 board requires only:
 *   1. A board header in boards/ with pin assignments
 *   2. An #elif clause in this file
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ARM_RP2350_DEVICE_SELECT_H_
#define TIKU_ARM_RP2350_DEVICE_SELECT_H_

/*---------------------------------------------------------------------------*/
/* DEVICE                                                                    */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_DEVICE_RP2350)
#include <arch/arm-rp2350/devices/tiku_device_rp2350.h>
#else
#error "No TikuOS RP2350 device selected. Define TIKU_DEVICE_RP2350."
#endif

/*---------------------------------------------------------------------------*/
/* BOARD                                                                     */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_BOARD_RPI_PICO2_W)
#include <arch/arm-rp2350/boards/tiku_board_rpi_pico2_w.h>
#elif defined(TIKU_BOARD_RPI_PICO2)
#include <arch/arm-rp2350/boards/tiku_board_rpi_pico2.h>
#else
/* Default to the Pico 2 W board if nothing is selected — this is the
 * primary supported board. */
#define TIKU_BOARD_RPI_PICO2_W 1
#include <arch/arm-rp2350/boards/tiku_board_rpi_pico2_w.h>
#endif

#endif /* TIKU_ARM_RP2350_DEVICE_SELECT_H_ */
