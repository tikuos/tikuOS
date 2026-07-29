/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nordic_mdk.h - vendored Nordic MDK register-map router.
 *
 * Every Nordic arch file that touches registers includes this rather than a
 * device-specific MDK entry point, so one #elif chain selects the right map from
 * the single TIKU_DEVICE_NRF54* macro the Makefile defines.
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
