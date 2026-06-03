/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_arch.c - RP2350 backend for the wake-source HAL
 *
 * Reads NVIC enable state to figure out which wake sources are
 * currently armed. Mirrors arch/msp430/tiku_wake_arch.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_wake_hal.h>
#include "tiku_rp2350_regs.h"
#include <string.h>

void tiku_wake_arch_query(tiku_wake_sources_t *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    uint32_t iser = *(volatile uint32_t *)RP2350_NVIC_ISER0;

    /* SysTick lives in the SCB (system exception 15). It is enabled
     * via the SYST_CSR.TICKINT bit; check that instead. */
    if (_RP2350_REG(RP2350_SYST_CSR) & RP2350_SYST_CSR_TICKINT) {
        out->sources |= TIKU_WAKE_SYSTICK;
    }

    if (iser & (1U << RP2350_IRQ_TIMER0_0)) {
        out->sources |= TIKU_WAKE_HTIMER;
    }
    if (iser & (1U << RP2350_IRQ_UART0)) {
        out->sources |= TIKU_WAKE_UART_RX;
    }
    if (iser & (1U << RP2350_IRQ_IO_BANK0)) {
        out->sources |= TIKU_WAKE_GPIO;
    }
    /* Watchdog reset is always armed when the watchdog is enabled —
     * the kernel's tiku_watchdog tracks its own enabled flag and the
     * "wake" command queries that separately, so we leave WDT cleared
     * here. */

    /* Per-port GPIO IE summary: scan PROC0_INTE for non-zero words.
     * Each word covers 8 pins (one virtual port). */
    uint8_t i;
    for (i = 0U; i < 4U && i < TIKU_WAKE_MAX_GPIO_PORTS; i++) {
        uint32_t inte = _RP2350_REG(RP2350_IO_BANK0_PROC0_INTE(i));
        /* Compress the 4-bits-per-pin word into a 1-bit-per-pin byte
         * so the snapshot fits the MSP430-style 8-bit layout. */
        uint8_t pinbits = 0U;
        uint8_t p;
        for (p = 0U; p < 8U; p++) {
            if ((inte >> (p * 4U)) & 0xFU) {
                pinbits |= (uint8_t)(1U << p);
            }
        }
        out->gpio_ie[i] = pinbits;
    }
}
