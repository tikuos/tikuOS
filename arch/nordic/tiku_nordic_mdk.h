/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nordic_mdk.h - vendored Nordic MDK register-map router
 *
 * Every Nordic arch .c that touches silicon registers includes THIS header
 * rather than a device-specific MDK entry point, so a single #elif chain
 * selects the right register map for the chosen device.  The Makefile defines
 * exactly one TIKU_DEVICE_NRF54* macro (derived from MCU=...).
 *
 * Adding a new nRF54L variant = vendor its <device>_types.h / <device>_global.h
 * under mdk/, add a TikuOS entry wrapper mdk/<device>.h (see nrf54l15.h /
 * nrf54lm20a.h), and add one #elif clause below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_MDK_H_
#define TIKU_NORDIC_MDK_H_

#if defined(TIKU_DEVICE_NRF54L15)
#include <arch/nordic/mdk/nrf54l15.h>
#elif defined(TIKU_DEVICE_NRF54LM20A)
#include <arch/nordic/mdk/nrf54lm20a.h>
#elif defined(TIKU_DEVICE_NRF54LM20B)
#include <arch/nordic/mdk/nrf54lm20b.h>
#else
#error "No TikuOS Nordic device selected. Define TIKU_DEVICE_NRF54L15, TIKU_DEVICE_NRF54LM20A or TIKU_DEVICE_NRF54LM20B."
#endif

#endif /* TIKU_NORDIC_MDK_H_ */
