/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - Apollo 510 device + board include router
 *
 * Mirrors arch/arm-rp2350/tiku_device_select.h. The Makefile sets:
 *   TIKU_DEVICE_APOLLO510       (silicon)
 *   TIKU_BOARD_APOLLO510_EVB    (board pin definitions)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_DEVICE_SELECT_H_
#define TIKU_AMBIQ_DEVICE_SELECT_H_

/*---------------------------------------------------------------------------*/
/* DEVICE                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Pull in silicon-level constants for the selected Ambiq device.
 *
 * The Makefile defines exactly one TIKU_DEVICE_* symbol. This block maps
 * that symbol to the corresponding device header. Add a new @c elif branch
 * here when porting to a new Ambiq silicon variant.
 */
#if defined(TIKU_DEVICE_APOLLO510B)
/* Apollo510 Blue (the "B"/lni EVB) is the SAME Apollo510 (AMAP510) Cortex-M55
 * silicon -- identical register map, memory sizes and peripherals -- with an
 * on-board EM9305 BLE radio the base EVB lacks.  Reuse the Apollo510 device
 * header and just override the human-facing name.  Listed first so it wins
 * even though tiku.h's fallback also defines TIKU_DEVICE_APOLLO510. */
#include <arch/ambiq/devices/tiku_device_apollo510.h>
#undef  TIKU_DEVICE_NAME
#define TIKU_DEVICE_NAME "Apollo510 Blue"
#elif defined(TIKU_DEVICE_APOLLO4P)
/* Apollo4 Plus (AMAP42KP) reuses the register-compatible Apollo4 Lite silicon
 * header (same Cortex-M4F Apollo4 peripheral map); only the human-facing name
 * and the memory sizes (2 MB MRAM / 2.75 MB SRAM) differ.  Listed first so it
 * wins even if a generic device fallback is also set. */
#include <arch/ambiq/devices/tiku_device_apollo4l.h>
#undef  TIKU_DEVICE_NAME
#define TIKU_DEVICE_NAME "Apollo4 Plus"
#elif defined(TIKU_DEVICE_APOLLO510)
#include <arch/ambiq/devices/tiku_device_apollo510.h>
#elif defined(TIKU_DEVICE_APOLLO4L)
#include <arch/ambiq/devices/tiku_device_apollo4l.h>
#else
#error "No TikuOS Ambiq device selected. Define TIKU_DEVICE_APOLLO510 or TIKU_DEVICE_APOLLO4L."
#endif

/*---------------------------------------------------------------------------*/
/* BOARD                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Pull in board-level GPIO pin assignments for the selected board.
 *
 * The Makefile may define TIKU_BOARD_APOLLO510_EVB. If no board is
 * specified the Apollo510 EVB is assumed — it is the only supported
 * board for this silicon at this milestone.
 */
#if defined(TIKU_BOARD_APOLLO510B_EVB)
#include <arch/ambiq/boards/tiku_board_apollo510b_evb.h>
#elif defined(TIKU_BOARD_APOLLO4L_EVB)
#include <arch/ambiq/boards/tiku_board_apollo4l_evb.h>
#elif defined(TIKU_BOARD_APOLLO510_EVB)
#include <arch/ambiq/boards/tiku_board_apollo510_evb.h>
#else
/* Default to the Apollo510 EVB. */
#define TIKU_BOARD_APOLLO510_EVB 1
#include <arch/ambiq/boards/tiku_board_apollo510_evb.h>
#endif

#endif /* TIKU_AMBIQ_DEVICE_SELECT_H_ */
