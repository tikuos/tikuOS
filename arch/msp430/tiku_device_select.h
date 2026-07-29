/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - device and board include router.
 *
 * Two separate choices: TIKU_DEVICE_* picks the silicon header (memory map,
 * ports, xtal) and TIKU_BOARD_* the PCB header (LEDs, buttons, headers).  A build
 * that names only the device falls back to that part's default LaunchPad.
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
