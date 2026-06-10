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

#if defined(TIKU_DEVICE_APOLLO510)
#include <arch/ambiq/devices/tiku_device_apollo510.h>
#else
#error "No TikuOS Ambiq device selected. Define TIKU_DEVICE_APOLLO510."
#endif

/*---------------------------------------------------------------------------*/
/* BOARD                                                                     */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_BOARD_APOLLO510_EVB)
#include <arch/ambiq/boards/tiku_board_apollo510_evb.h>
#else
/* Default to the Apollo510 EVB — the only supported board for now. */
#define TIKU_BOARD_APOLLO510_EVB 1
#include <arch/ambiq/boards/tiku_board_apollo510_evb.h>
#endif

#endif /* TIKU_AMBIQ_DEVICE_SELECT_H_ */
