/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_hal.h - platform-agnostic wake-source query interface.
 *
 * Reports which interrupt families are armed, and so can wake the CPU from a
 * low-power state, for the `wake` command and /sys/power/wake.  Each port maps
 * a flag to the interrupt-enable registers behind it.
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
 * Number of GPIO ports whose per-pin enable masks the snapshot carries:
 * MSP430's P1..P4.  An entry a port does not fill stays zero.
 */
#define TIKU_WAKE_MAX_GPIO_PORTS 4

/**
 * @struct tiku_wake_sources_t
 * @brief Snapshot of currently-armed wake sources.
 *
 * `sources` is a bit-OR of the TIKU_WAKE_* flags; `gpio_ie[i]` is the per-pin
 * IE mask for port i+1 and is valid only when TIKU_WAKE_GPIO is set.
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
 *
 * Returns immediately, with no side effect on the hardware.
 *
 * @param out  Destination snapshot. Cleared then populated.
 * @note Reads volatile peripheral state: call from non-ISR context for a
 *       coherent snapshot.
 */
void tiku_wake_arch_query(tiku_wake_sources_t *out);

#endif /* TIKU_WAKE_HAL_H_ */
