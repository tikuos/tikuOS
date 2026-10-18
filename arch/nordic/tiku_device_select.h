/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - Nordic nRF54L device and board include router.
 *
 * The Makefile names the silicon (TIKU_DEVICE_*) and the board
 * (TIKU_BOARD_*) separately.  A new device or board needs a header under
 * devices/ or boards/ and one #elif here.
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
 * The Makefile must define one TIKU_DEVICE_NRF54* macro (derived from MCU=...).
 * Other nRF54L silicon variants add an @c #elif here and a matching device
 * header under devices/.
 */
#if defined(TIKU_DEVICE_NRF54L15)
#include <arch/nordic/devices/tiku_device_nrf54l15.h>
#elif defined(TIKU_DEVICE_NRF54LM20A)
#include <arch/nordic/devices/tiku_device_nrf54lm20a.h>
#elif defined(TIKU_DEVICE_NRF54LM20B)
#include <arch/nordic/devices/tiku_device_nrf54lm20b.h>
#elif defined(TIKU_DEVICE_NRF54LV10A)
#include <arch/nordic/devices/tiku_device_nrf54lv10a.h>
#else
#error "No TikuOS Nordic device selected. Define TIKU_DEVICE_NRF54L15, TIKU_DEVICE_NRF54LM20A, TIKU_DEVICE_NRF54LM20B or TIKU_DEVICE_NRF54LV10A."
#endif

/*---------------------------------------------------------------------------*/
/* BOARD                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Route the board-level pin-assignment header.
 *
 * TIKU_BOARD_NRF54L15_DK, _NRF54LM20_DK and _NRF54LV10_DK select the PCA10156,
 * PCA10184 and PCA10188 DKs.  When no board is defined, each device falls
 * back to its own DK as the primary board.
 */
#if defined(TIKU_BOARD_NRF54L15_DK)
#include <arch/nordic/boards/tiku_board_nrf54l15_dk.h>
#elif defined(TIKU_BOARD_NRF54LM20_DK)
#include <arch/nordic/boards/tiku_board_nrf54lm20_dk.h>
#elif defined(TIKU_BOARD_NRF54LV10_DK)
#include <arch/nordic/boards/tiku_board_nrf54lv10_dk.h>
#elif defined(TIKU_DEVICE_NRF54LV10A)
#define TIKU_BOARD_NRF54LV10_DK 1
#include <arch/nordic/boards/tiku_board_nrf54lv10_dk.h>
#elif defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
/* Default board for both LM20 variants: the nRF54LM20-DK (PCA10184 ships
 * LM20B engineering silicon; the LM20A image runs on it too). */
#define TIKU_BOARD_NRF54LM20_DK 1
#include <arch/nordic/boards/tiku_board_nrf54lm20_dk.h>
#else
/* Default to the nRF54L15-DK board -- the primary supported board. */
#define TIKU_BOARD_NRF54L15_DK 1
#include <arch/nordic/boards/tiku_board_nrf54l15_dk.h>
#endif

#endif /* TIKU_NORDIC_DEVICE_SELECT_H_ */
