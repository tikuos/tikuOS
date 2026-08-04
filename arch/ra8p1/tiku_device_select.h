/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - route RA8P1 builds to their device and board headers.
 *
 * The Makefile defines TIKU_DEVICE_RA8P1 from the MCU name; another RA variant
 * adds an #elif here and a header under devices/.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_DEVICE_SELECT_H_
#define TIKU_RA8P1_DEVICE_SELECT_H_

#if defined(TIKU_DEVICE_RA8P1)
#include <arch/ra8p1/devices/tiku_device_ra8p1.h>
#else
#error "No TikuOS RA8P1 device selected. Define TIKU_DEVICE_RA8P1."
#endif

/*---------------------------------------------------------------------------*/
/* BOARD                                                                     */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_BOARD_EK_RA8P1)
#include <arch/ra8p1/boards/tiku_board_ek_ra8p1.h>
#else
#error "No TikuOS RA8P1 board selected. Define TIKU_BOARD_EK_RA8P1."
#endif

#endif /* TIKU_RA8P1_DEVICE_SELECT_H_ */
