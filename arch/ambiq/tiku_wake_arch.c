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
 * Cortex-M register reads.  Both parts take the tick from the always-on
 * STIMER, as SysTick stops in sleep; only the console UART IRQ differs.
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
#define AMBIQ_IRQ_UART           16   /**< UART1 console RX (apollo510b Blue) */
#else
#define AMBIQ_IRQ_UART           15   /**< UART0 console RX (apollo510, 4p) */
#endif
#define AMBIQ_IRQ_STIMER_CMPR0   32   /**< STIMER Compare0 (htimer)  */
#define AMBIQ_IRQ_STIMER_CMPR1   33   /**< STIMER Compare1 (kernel tick) */
#define AMBIQ_IRQ_GPIO0_FIRST    56   /**< First GPIO N0 IRQ line    */
#define AMBIQ_IRQ_GPIO0_LAST     63   /**< Last GPIO N0 IRQ line     */

/** @brief True if external IRQ @p irq is enabled in the NVIC. */
static int irq_enabled(unsigned irq) {
    return (NVIC_ISER[irq >> 5] & (1u << (irq & 31u))) != 0u;
}

/**
 * @brief Query the wake sources currently armed
 *
 * Sets a TIKU_WAKE_* flag for each enabled NVIC line: SYSTICK for the STIMER
 * tick (compare-B), HTIMER for compare-A, UART_RX for the console UART and
 * GPIO for any GPIO0 line.  A NULL @p out is a no-op.
 *
 * @note The Apollo510 watchdog (NVIC IRQ 1, often left enabled by the SBL) is
 *       the reset watchdog, not the interval-interrupt wake source
 *       TIKU_WAKE_WDT denotes, so it is not reported.
 * @param out  Wake-source snapshot to populate
 */
void tiku_wake_arch_query(tiku_wake_sources_t *out) {
    unsigned g;
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    /* On both Ambiq parts the kernel tick is the STIMER compare-B interrupt
     * (IRQ 33): SysTick stops in WFI and runs without TICKINT, so
     * TIKU_WAKE_SYSTICK reports that NVIC line (tiku_timer_*.c). */
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
