/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_arch.c - Ambiq wake-source query.
 *
 * Reports which sources are armed to bring the core out of WFI, from pure
 * Cortex-M register reads.  Both parts tick off the always-on STIMER, since
 * SysTick freezes in sleep; only the console UART IRQ number differs per part.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <hal/tiku_wake_hal.h>

/** NVIC Interrupt Set-Enable Registers (ISER[0..4] cover the 135 IRQs). */
#define NVIC_ISER         ((volatile uint32_t *)0xE000E100UL)

/* IRQ numbers for the lines tikuOS maps to wake sources. The console UART
 * differs per Ambiq part; STIMER and the GPIO0 range are identical. */
#if defined(TIKU_DEVICE_APOLLO4L)
#define AMBIQ_IRQ_UART           17   /**< UART2 console RX IRQ (apollo4l) */
#elif defined(TIKU_CONSOLE_UART1)
#define AMBIQ_IRQ_UART           16   /**< UART1 console RX IRQ (apollo510b Blue) */
#else
#define AMBIQ_IRQ_UART           15   /**< UART0 console RX IRQ (apollo510) */
#endif
#define AMBIQ_IRQ_STIMER_CMPR0   32   /**< STIMER Compare0 (htimer)  */
#define AMBIQ_IRQ_STIMER_CMPR1   33   /**< STIMER Compare1 (Ambiq kernel tick) */
#define AMBIQ_IRQ_GPIO0_FIRST    56   /**< First GPIO N0 IRQ line    */
#define AMBIQ_IRQ_GPIO0_LAST     63   /**< Last GPIO N0 IRQ line     */

/** True if external IRQ @p irq is enabled in the NVIC. */
static int irq_enabled(unsigned irq) {
    return (NVIC_ISER[irq >> 5] & (1u << (irq & 31u))) != 0u;
}

/**
 * @brief Query the wake sources currently armed
 *
 * Populates @p out->sources with the TIKU_WAKE_* flags whose underlying
 * interrupt is enabled: SysTick (SYST_CSR.TICKINT) and the NVIC-enabled STIMER
 * / UART0 / GPIO0 lines.  A NULL @p out is a no-op.
 *
 * @note The Apollo510 watchdog (NVIC IRQ 1, often left enabled by the SBL) is
 *       the reset watchdog, not the interval-interrupt wake source TIKU_WAKE_WDT
 *       denotes, so it is intentionally not reported.
 * @param out  Wake-source snapshot to populate
 */
void tiku_wake_arch_query(tiku_wake_sources_t *out) {
    unsigned g;
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    /* Both Ambiq parts drive the kernel tick from the always-on STIMER compare-B
     * (IRQ 33), not SysTick -- SysTick freezes in WFI sleep, so it carries no
     * TICKINT and the tick wake source is that NVIC line (tiku_timer_*.c). */
    if (irq_enabled(AMBIQ_IRQ_STIMER_CMPR1)) {
        out->sources |= TIKU_WAKE_SYSTICK;
    }
    if (irq_enabled(AMBIQ_IRQ_STIMER_CMPR0)) {
        out->sources |= TIKU_WAKE_HTIMER;
    }
    if (irq_enabled(AMBIQ_IRQ_UART)) {
        out->sources |= TIKU_WAKE_UART_RX;
    }
    for (g = AMBIQ_IRQ_GPIO0_FIRST; g <= AMBIQ_IRQ_GPIO0_LAST; g++) {
        if (irq_enabled(g)) {
            out->sources |= TIKU_WAKE_GPIO;
            break;
        }
    }
}
