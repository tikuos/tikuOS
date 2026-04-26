/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_hal.h - Platform-agnostic wake-source query interface
 *
 * Used by the "wake" shell command and /sys/power/wake VFS node to
 * report which interrupt families are currently armed and would
 * therefore wake the CPU from a low-power state.
 *
 * The HAL exposes a fixed set of role-named flags. The arch backend
 * is free to map each flag to whichever device-specific IE register
 * (or registers) covers that role on the current MCU.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_WAKE_HAL_H_
#define TIKU_WAKE_HAL_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* WAKE-SOURCE FLAGS                                                         */
/*---------------------------------------------------------------------------*/

/** System tick timer (e.g. Timer A0 on MSP430). */
#define TIKU_WAKE_SYSTICK   (1u << 0)

/** Hardware (high-resolution) timer (e.g. Timer A1 on MSP430). */
#define TIKU_WAKE_HTIMER    (1u << 1)

/** UART receive interrupt. */
#define TIKU_WAKE_UART_RX   (1u << 2)

/** Watchdog interval-mode interrupt (when WDT is used as a timer). */
#define TIKU_WAKE_WDT       (1u << 3)

/** Any GPIO pin-edge interrupt is enabled on the device. */
#define TIKU_WAKE_GPIO      (1u << 4)

/*---------------------------------------------------------------------------*/
/* OPTIONAL PER-PORT GPIO DETAIL                                             */
/*---------------------------------------------------------------------------*/

/**
 * Maximum GPIO ports the wake snapshot reports per-port enable
 * masks for. Sized for MSP430's P1..P4. Other platforms can leave
 * the unused entries zero.
 */
#define TIKU_WAKE_MAX_GPIO_PORTS 4

/**
 * @struct tiku_wake_sources_t
 * @brief Snapshot of currently-armed wake sources.
 *
 * sources    -- bit-OR of TIKU_WAKE_* flags above.
 * gpio_ie[i] -- per-port pin-IE mask for port (i+1) if that port
 *               exists and any pin in it has an interrupt enabled.
 *               Valid only when TIKU_WAKE_GPIO is set in @p sources.
 */
typedef struct {
    uint8_t sources;
    uint8_t gpio_ie[TIKU_WAKE_MAX_GPIO_PORTS];
} tiku_wake_sources_t;

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTION                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Snapshot the platform's currently-armed wake sources.
 * @param out  Destination snapshot. Cleared then populated.
 *
 * Reads volatile peripheral state, so call from a non-ISR context
 * if a coherent picture matters. Returns immediately; no side
 * effects on the hardware.
 */
void tiku_wake_arch_query(tiku_wake_sources_t *out);

#endif /* TIKU_WAKE_HAL_H_ */
