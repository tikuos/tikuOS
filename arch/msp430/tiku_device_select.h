/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - Device and board include router
 *
 * This header routes to the correct device and board headers. The two
 * are SEPARATE choices:
 *
 *   TIKU_DEVICE_*  the silicon        -> devices/ (memory map, ports, xtal)
 *   TIKU_BOARD_*   the physical PCB   -> boards/  (LEDs, buttons, headers)
 *
 * The device selects the device header; the board selects the board
 * header.  They were fused here until the board/device split -- an
 * FR5994 could only ever be a LaunchPad, which is wrong the moment the
 * same part sits on a custom TikuOS board.
 *
 * Builds that pass no TIKU_BOARD_* (TI Code Composer Studio, which sets
 * only the device via __MSP430FR*__) fall back to that device's default
 * LaunchPad, so nothing outside the Makefile has to change.
 *
 * Adding a new MSP430 variant requires:
 *   1. A device header in devices/ with silicon constants
 *   2. A board header in boards/ with GPIO pin assignments
 *   3. An #elif clause in each of the two routers below
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
/* DEVICE ROUTER -- the silicon                                              */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_DEVICE_MSP430FR5969)
#include <arch/msp430/devices/tiku_device_fr5969.h>
#elif defined(TIKU_DEVICE_MSP430FR5994)
#include <arch/msp430/devices/tiku_device_fr5994.h>
#elif defined(TIKU_DEVICE_MSP430FR6989)
#include <arch/msp430/devices/tiku_device_fr6989.h>
#elif defined(TIKU_DEVICE_MSP430FR2433)
#include <arch/msp430/devices/tiku_device_fr2433.h>
#else
#error "No TikuOS device selected. Define TIKU_DEVICE_MSP430FR5994 (or another supported device) in tiku.h"
#endif

/*---------------------------------------------------------------------------*/
/* BOARD DEFAULT -- only when the build system named no board                 */
/*---------------------------------------------------------------------------*/
/*
 * The Makefile always passes -DTIKU_BOARD_* (see BOARD_DEFINE_* there).
 * This block exists for the CCS project, which sets the device only.
 */

#if !defined(TIKU_BOARD_FR5969_LAUNCHPAD) && \
    !defined(TIKU_BOARD_FR5994_LAUNCHPAD) && \
    !defined(TIKU_BOARD_FR6989_LAUNCHPAD) && \
    !defined(TIKU_BOARD_FR2433_LAUNCHPAD)

#if defined(TIKU_DEVICE_MSP430FR5969)
#define TIKU_BOARD_FR5969_LAUNCHPAD 1
#elif defined(TIKU_DEVICE_MSP430FR5994)
#define TIKU_BOARD_FR5994_LAUNCHPAD 1
#elif defined(TIKU_DEVICE_MSP430FR6989)
#define TIKU_BOARD_FR6989_LAUNCHPAD 1
#elif defined(TIKU_DEVICE_MSP430FR2433)
#define TIKU_BOARD_FR2433_LAUNCHPAD 1
#endif

#endif

/*---------------------------------------------------------------------------*/
/* BOARD ROUTER -- the PCB                                                   */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_BOARD_FR5969_LAUNCHPAD)
#include <arch/msp430/boards/tiku_board_fr5969_launchpad.h>
#elif defined(TIKU_BOARD_FR5994_LAUNCHPAD)
#include <arch/msp430/boards/tiku_board_fr5994_launchpad.h>
#elif defined(TIKU_BOARD_FR6989_LAUNCHPAD)
#include <arch/msp430/boards/tiku_board_fr6989_launchpad.h>
#elif defined(TIKU_BOARD_FR2433_LAUNCHPAD)
#include <arch/msp430/boards/tiku_board_fr2433_launchpad.h>
#else
#error "No TikuOS board selected. Pass BOARD=<name> to make (see KNOWN_BOARDS in the Makefile)."
#endif

#endif /* TIKU_DEVICE_SELECT_H_ */
