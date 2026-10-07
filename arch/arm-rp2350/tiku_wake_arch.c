/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_arch.c - RP2350 backend for the wake-source HAL
 *
 * Reports which wake sources are armed, from the NVIC enables, SysTick's
 * TICKINT bit and the IO_BANK0 PROC0_INTE registers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_wake_hal.h>
#include "tiku_rp2350_regs.h"
#include <string.h>

/**
 * @brief Query the currently armed wake sources on RP2350.
 *
 * Maps SysTick TICKINT -> SYSTICK and the NVIC enables of TIMER0_0 -> HTIMER,
 * UART0 -> UART_RX and IO_BANK0 -> GPIO; gpio_ie[] gets one bit per pin for
 * GPIO 0-31, 8 pins per byte, from PROC0_INTE.
 *
 * @note TIKU_WAKE_WDT is never set: the RP2350 watchdog resets the chip and
 *       raises no interrupt.
 * @param out  Destination for wake source bitmap (must be non-NULL).
 *             If NULL the function returns immediately.
 */
void tiku_wake_arch_query(tiku_wake_sources_t *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    uint32_t iser = *(volatile uint32_t *)RP2350_NVIC_ISER0;

    /* SysTick is system exception 15, outside the NVIC; SYST_CSR.TICKINT
     * enables its interrupt. */
    if (_RP2350_REG(RP2350_SYST_CSR) & RP2350_SYST_CSR_TICKINT) {
        out->sources |= TIKU_WAKE_SYSTICK;
    }

    if (iser & (1U << RP2350_IRQ_TIMER0_0)) {
        out->sources |= TIKU_WAKE_HTIMER;
    }
    uint32_t uart_iser = _RP2350_REG(RP2350_NVIC_ISER0 +
                                     (RP2350_IRQ_UART0 / 32U) * 4U);
    if (uart_iser & (1U << (RP2350_IRQ_UART0 & 31U))) {
        out->sources |= TIKU_WAKE_UART_RX;
    }
    if (iser & (1U << RP2350_IRQ_IO_BANK0)) {
        out->sources |= TIKU_WAKE_GPIO;
    }
    /* Per-port GPIO IE summary from PROC0_INTE.  Each word covers 8 pins,
     * reported as one gpio_ie[] byte. */
    uint8_t i;
    for (i = 0U; i < 4U && i < TIKU_WAKE_MAX_GPIO_PORTS; i++) {
        uint32_t inte = _RP2350_REG(RP2350_IO_BANK0_PROC0_INTE(i));
        /* A pin with any of its four enable bits set gets its bit. */
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
