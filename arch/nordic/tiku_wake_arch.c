/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wake_arch.c - nRF54L backend for the wake-source HAL
 *
 * Reports which wake sources are armed by inspecting the NVIC enable state
 * of the live interrupt lines (all IRQ numbers are the MDK IRQn enum):
 *
 *   GRTC 226 (default tick) or TIMER10 133 (fallback) -> TIKU_WAKE_SYSTICK
 *   TIMER20 202 (hardware one-shot htimer)            -> TIKU_WAKE_HTIMER
 *   SERIAL20 198 / SERIAL30 260 (console UARTE RX)    -> TIKU_WAKE_UART_RX
 *   GPIOTE20 218 / GPIOTE30 268 (pin edges)           -> TIKU_WAKE_GPIO
 *
 * The WDT wake bit is left clear intentionally: WDT30 runs in reset mode
 * (no NVIC line armed) and the kernel's tiku_watchdog layer tracks the
 * armed state separately -- same convention as the rp2350 backend.
 *
 * gpio_ie[] is a best-effort per-port summary built by scanning the GPIOTE
 * event-channel CONFIG registers (MODE=Event entries carry the pin+port),
 * compressed into the HAL's 8-bit-per-port layout (bit = pin % 8).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_wake_hal.h>
#include <arch/nordic/tiku_nordic_core.h>
#include <arch/nordic/tiku_nordic_mdk.h>
#include <string.h>

/* Live NVIC lines (MDK IRQn enum values). */
#define TIKU_NORDIC_IRQ_GRTC      226u   /* GRTC_0: default kernel tick     */
#define TIKU_NORDIC_IRQ_TIMER10   133u   /* TIMER10: tick fallback build    */
#define TIKU_NORDIC_IRQ_TIMER20   202u   /* TIMER20: htimer one-shot        */
#define TIKU_NORDIC_IRQ_SERIAL20  198u   /* UARTE20 console (SEL 20)        */
#define TIKU_NORDIC_IRQ_SERIAL30  260u   /* UARTE30 console (SEL 30)        */
#define TIKU_NORDIC_IRQ_GPIOTE20  218u   /* GPIOTE20 line 0 (P1/P2 edges)   */
#define TIKU_NORDIC_IRQ_GPIOTE30  268u   /* GPIOTE30 line 0 (P0 edges)      */

/* GPIOTE CONFIG decode (same encoding the gpio_irq backend programs). */
#define TIKU_GPIOTE_NCH           8u
#define TIKU_GPIOTE_MODE_MSK      0x3UL
#define TIKU_GPIOTE_MODE_EVENT    0x1UL
#define TIKU_GPIOTE_PSEL_POS      4u
#define TIKU_GPIOTE_PSEL_MSK      0x1FUL
#define TIKU_GPIOTE_PORT_POS      9u
#define TIKU_GPIOTE_PORT_MSK      0xFUL

/** @brief Non-zero when NVIC line @p irqn is enabled. */
static uint32_t nvic_line_armed(uint32_t irqn)
{
    return TIKU_NVIC->ISER[irqn >> 5] & (1UL << (irqn & 0x1Fu));
}

/**
 * @brief Fold one GPIOTE instance's armed event channels into gpio_ie[].
 *
 * Scans CONFIG[0..7]; every MODE=Event channel contributes its pin to the
 * owning port's IE byte (bit = pin % 8 -- the HAL layout is 8 bits/port).
 */
static void gpiote_fold(NRF_GPIOTE_Type *reg, tiku_wake_sources_t *out)
{
    uint8_t ch;

    for (ch = 0u; ch < TIKU_GPIOTE_NCH; ch++) {
        uint32_t cfg = reg->CONFIG[ch];

        if ((cfg & TIKU_GPIOTE_MODE_MSK) == TIKU_GPIOTE_MODE_EVENT) {
            uint8_t pin  = (uint8_t)((cfg >> TIKU_GPIOTE_PSEL_POS)
                                     & TIKU_GPIOTE_PSEL_MSK);
            uint8_t port = (uint8_t)((cfg >> TIKU_GPIOTE_PORT_POS)
                                     & TIKU_GPIOTE_PORT_MSK);

            if (port < TIKU_WAKE_MAX_GPIO_PORTS) {
                out->gpio_ie[port] |= (uint8_t)(1u << (pin & 7u));
            }
        }
    }
}

void tiku_wake_arch_query(tiku_wake_sources_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    /* Kernel tick: GRTC by default, TIMER10 under the fallback build. */
    if (nvic_line_armed(TIKU_NORDIC_IRQ_GRTC) ||
        nvic_line_armed(TIKU_NORDIC_IRQ_TIMER10)) {
        out->sources |= TIKU_WAKE_SYSTICK;
    }

    /* Hardware one-shot htimer (TIMER20 compare). */
    if (nvic_line_armed(TIKU_NORDIC_IRQ_TIMER20)) {
        out->sources |= TIKU_WAKE_HTIMER;
    }

    /* Console UARTE RX (whichever VCOM the board selected). */
    if (nvic_line_armed(TIKU_NORDIC_IRQ_SERIAL20) ||
        nvic_line_armed(TIKU_NORDIC_IRQ_SERIAL30)) {
        out->sources |= TIKU_WAKE_UART_RX;
    }

    /* GPIO edges: a GPIOTE line armed in the NVIC with at least one
     * event channel configured. */
    if (nvic_line_armed(TIKU_NORDIC_IRQ_GPIOTE20)) {
        gpiote_fold(NRF_GPIOTE20_S, out);
    }
    if (nvic_line_armed(TIKU_NORDIC_IRQ_GPIOTE30)) {
        gpiote_fold(NRF_GPIOTE30_S, out);
    }
    {
        uint8_t i;

        for (i = 0u; i < TIKU_WAKE_MAX_GPIO_PORTS; i++) {
            if (out->gpio_ie[i] != 0u) {
                out->sources |= TIKU_WAKE_GPIO;
                break;
            }
        }
    }
}
