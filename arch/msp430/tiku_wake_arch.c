/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_arch.c - MSP430 backend for the wake-source HAL
 *
 * Maps each TIKU_WAKE_* role flag to whichever MSP430 IE register
 * covers it on the current device. Each register access is guarded
 * by #if defined() so the file compiles unchanged across FR5969,
 * FR5994, FR2433, etc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_wake_hal.h>
#include <msp430.h>
#include <string.h>

void
tiku_wake_arch_query(tiku_wake_sources_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));

    /* System tick (Timer A0) */
    if ((TA0CTL & MC__UP) != 0) {
        out->sources |= TIKU_WAKE_SYSTICK;
    }

    /* Hardware timer (Timer A1) */
    if ((TA1CTL & MC__UP) != 0) {
        out->sources |= TIKU_WAKE_HTIMER;
    }

    /* UART RX (eUSCI_A0) -- only family the shell currently exposes */
#if defined(UCA0IE) && defined(UCRXIE)
    if ((UCA0IE & UCRXIE) != 0) {
        out->sources |= TIKU_WAKE_UART_RX;
    }
#endif

    /* Watchdog interval-mode interrupt */
#if defined(SFRIE1) && defined(WDTIE)
    if ((SFRIE1 & WDTIE) != 0) {
        out->sources |= TIKU_WAKE_WDT;
    }
#endif

    /* GPIO edge IRQs across P1..P4. The HAL caps the per-port array
     * at TIKU_WAKE_MAX_GPIO_PORTS = 4, which covers every supported
     * MSP430 variant. */
#if defined(P1IE)
    out->gpio_ie[0] = P1IE;
#endif
#if defined(P2IE)
    out->gpio_ie[1] = P2IE;
#endif
#if defined(P3IE)
    out->gpio_ie[2] = P3IE;
#endif
#if defined(P4IE)
    out->gpio_ie[3] = P4IE;
#endif

    if (out->gpio_ie[0] | out->gpio_ie[1] |
        out->gpio_ie[2] | out->gpio_ie[3]) {
        out->sources |= TIKU_WAKE_GPIO;
    }
}
