/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - Nordic nRF54L device + board include router
 *
 * Mirrors arch/msp430/tiku_device_select.h.  The Makefile sets one of:
 *   TIKU_DEVICE_NRF54L15       (silicon)
 *   TIKU_BOARD_NRF54L15_DK     (board PCB definitions)
 *
 * Adding a new nRF54L board requires only a board header under boards/ and
 * an #elif clause below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_DEVICE_SELECT_H_
#define TIKU_NORDIC_DEVICE_SELECT_H_

/*---------------------------------------------------------------------------*/
/* DEVICE                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Route the silicon-level device header.
 *
 * The Makefile must define TIKU_DEVICE_NRF54L15.  Other nRF54L silicon
 * variants would add an @c #elif here and a matching device header under
 * devices/.
 */
#if defined(TIKU_DEVICE_NRF54L15)
#include <arch/nordic/devices/tiku_device_nrf54l15.h>
#else
#error "No TikuOS Nordic device selected. Define TIKU_DEVICE_NRF54L15."
#endif

/*---------------------------------------------------------------------------*/
/* BOARD                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Route the board-level pin-assignment header.
 *
 * TIKU_BOARD_NRF54L15_DK selects the nRF54L15-DK (PCA10156).  When nothing
 * is defined the DK is used as the default primary supported board.
 */
#if defined(TIKU_BOARD_NRF54L15_DK)
#include <arch/nordic/boards/tiku_board_nrf54l15_dk.h>
#else
/* Default to the nRF54L15-DK board -- the primary supported board. */
#define TIKU_BOARD_NRF54L15_DK 1
#include <arch/nordic/boards/tiku_board_nrf54l15_dk.h>
#endif

#endif /* TIKU_NORDIC_DEVICE_SELECT_H_ */
