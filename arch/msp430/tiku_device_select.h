/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - Device and board include router
 *
 * This header routes to the correct device and board headers based
 * on the TIKU_DEVICE_* define set in tiku.h. Adding a new MSP430
 * variant requires only:
 *   1. A device header in devices/ with silicon constants
 *   2. A board header in boards/ with GPIO pin assignments
 *   3. An #elif clause in this file
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_SELECT_H_
#define TIKU_DEVICE_SELECT_H_

/*---------------------------------------------------------------------------*/
/* AUTO-DETECT FROM TI COMPILER DEFINES                                      */
/*---------------------------------------------------------------------------*/

#if defined(__MSP430FR5969__) && !defined(TIKU_DEVICE_MSP430FR5969)
#define TIKU_DEVICE_MSP430FR5969 1
#elif defined(__MSP430FR5994__) && !defined(TIKU_DEVICE_MSP430FR5994)
#define TIKU_DEVICE_MSP430FR5994 1
#elif defined(__MSP430FR6989__) && !defined(TIKU_DEVICE_MSP430FR6989)
#define TIKU_DEVICE_MSP430FR6989 1
#elif defined(__MSP430FR2433__) && !defined(TIKU_DEVICE_MSP430FR2433)
#define TIKU_DEVICE_MSP430FR2433 1
#endif

/*---------------------------------------------------------------------------*/
/* DEVICE SELECTION ROUTER                                                   */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_DEVICE_MSP430FR5969)

#include <arch/msp430/devices/tiku_device_fr5969.h>
#include <arch/msp430/boards/tiku_board_fr5969_launchpad.h>

#elif defined(TIKU_DEVICE_MSP430FR5994)

#include <arch/msp430/devices/tiku_device_fr5994.h>
#include <arch/msp430/boards/tiku_board_fr5994_launchpad.h>

#elif defined(TIKU_DEVICE_MSP430FR6989)

#include <arch/msp430/devices/tiku_device_fr6989.h>
#include <arch/msp430/boards/tiku_board_fr6989_launchpad.h>

#elif defined(TIKU_DEVICE_MSP430FR2433)

#include <arch/msp430/devices/tiku_device_fr2433.h>
#include <arch/msp430/boards/tiku_board_fr2433_launchpad.h>

#else

#error "No TikuOS device selected. Define TIKU_DEVICE_MSP430FR5969 (or another supported device) in tiku.h"

#endif

#endif /* TIKU_DEVICE_SELECT_H_ */
