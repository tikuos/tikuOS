/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_select.h - Ambiq device and board include router.
 *
 * The Makefile defines one TIKU_DEVICE_* symbol from MCU= and one
 * TIKU_BOARD_* symbol from BOARD=; this header includes the matching device
 * and board headers.
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
 * A new Ambiq part needs its device header and an @c elif branch here.
 */
#if defined(TIKU_DEVICE_APOLLO510B)
/* The Apollo510B has the Apollo510 die (same register map, memory and
 * peripherals) plus an EM9305 BLE die, so it takes the Apollo510 header
 * under its own name.  It is tested first because tiku.h also defines
 * TIKU_DEVICE_APOLLO510 for this part. */
#include <arch/ambiq/devices/tiku_device_apollo510.h>
#undef  TIKU_DEVICE_NAME
#define TIKU_DEVICE_NAME "Apollo510 Blue"
#elif defined(TIKU_DEVICE_APOLLO4P)
/* The Apollo4 Plus (AMAP42KP) is register-compatible with the Apollo4 Lite
 * and takes its header under its own name; apollo4p.ld maps its larger
 * shared SRAM. */
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
 * @brief Pull in board-level pad assignments for the selected board.
 *
 * The Makefile passes one TIKU_BOARD_* (BOARD_DEFINE_* there).  A build that
 * defines none gets the Apollo510 EVB.
 */
#if defined(TIKU_BOARD_TIKU_BARE)
#include <arch/ambiq/boards/tiku_board_tiku_bare.h>
#elif defined(TIKU_BOARD_APOLLO510B_EVB)
#include <arch/ambiq/boards/tiku_board_apollo510b_evb.h>
#elif defined(TIKU_BOARD_APOLLO4P_EVB)
#include <arch/ambiq/boards/tiku_board_apollo4p_evb.h>
#elif defined(TIKU_BOARD_APOLLO4L_EVB)
#include <arch/ambiq/boards/tiku_board_apollo4l_evb.h>
#elif defined(TIKU_BOARD_APOLLO510_EVB)
#include <arch/ambiq/boards/tiku_board_apollo510_evb.h>
#else
/* No board selected: the Apollo510 EVB. */
#define TIKU_BOARD_APOLLO510_EVB 1
#include <arch/ambiq/boards/tiku_board_apollo510_evb.h>
#endif

#endif /* TIKU_AMBIQ_DEVICE_SELECT_H_ */
