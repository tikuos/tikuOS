/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - RP2350 device and board include router.
 *
 * Mirrors the MSP430 router: the Makefile names the silicon and the board PCB
 * separately.  A new board needs a header in boards/ and one #elif here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ARM_RP2350_DEVICE_SELECT_H_
#define TIKU_ARM_RP2350_DEVICE_SELECT_H_

/*---------------------------------------------------------------------------*/
/* DEVICE                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Route the silicon-level device header.
 *
 * The Makefile must define TIKU_DEVICE_RP2350. Any other RP2350
 * silicon variant would add an @c #elif here and a matching device
 * header under devices/.
 */
#if defined(TIKU_DEVICE_RP2350)
#include <arch/arm-rp2350/devices/tiku_device_rp2350.h>
#else
#error "No TikuOS RP2350 device selected. Define TIKU_DEVICE_RP2350."
#endif

/*---------------------------------------------------------------------------*/
/* BOARD                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Route the board-level pin-assignment header.
 *
 * TIKU_BOARD_RPI_PICO2_W selects the Pico 2 W (CYW43439 footprint);
 * TIKU_BOARD_RPI_PICO2 selects the plain Pico 2 (direct GP25 LED, no wireless).
 * With neither defined the Pico 2 W is the default.
 */
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
