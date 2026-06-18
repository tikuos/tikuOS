/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_apollo4l.c - Apollo4 Lite GPIO edge interrupts
 *
 * Bridges per-pin edge interrupts into TIKU_EVENT_GPIO broadcast events, the
 * same contract as the RP2350 backend (arch/arm-rp2350/tiku_gpio_irq_arch.c).
 * tikuOS addresses GPIO as virtual (port 1.., pin 0-7) -> pad (port-1)*8+pin
 * (matching tiku_gpio_apollo4l.c). Only pads 0-31 are wired here: they raise
 * GPIO0_001F_IRQn (56) via the GPIO->MCUN0INT0{EN,STAT,CLR} mask registers.
 * Higher pads (other IRQ vectors 60-63) report UNSUP.
 *
 * Edge select lives in the pad's PINCFG register eIntDir field [7:6]
 * (HI2LO=1 falling, LO2HI=2 rising, BOTH=3), written under the PADKEY lock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>
#include "apollo4l.h"            /* GPIO struct, NVIC, GPIO0_001F_IRQn */
#include <kernel/process/tiku_process.h>
#include <kernel/process/tiku_proto.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* PINCFG field constants (apollo4l GPIO->PINCFG0[pad])                       */
/*---------------------------------------------------------------------------*/

#define TIKU_GPIO_FNCSEL_GPIO    3u           /**< FNCSEL[3:0] = GPIO        */
#define TIKU_GPIO_INPEN          (1u << 4)    /**< eGPInput[4] input enable  */
#define TIKU_GPIO_INTDIR_POS     6u           /**< eIntDir[7:6] edge select  */
#define TIKU_GPIO_PADKEY_UNLOCK  0x73u        /**< GPIO_PADKEY unlock value  */

/** eIntDir encodings (am_hal_gpio.h AM_HAL_GPIO_PIN_INTDIR_*). */
#define TIKU_INTDIR_NONE   0u
#define TIKU_INTDIR_HI2LO  1u   /**< high->low (falling) */
#define TIKU_INTDIR_LO2HI  2u   /**< low->high (rising)  */
#define TIKU_INTDIR_BOTH   3u

/** Highest pad that routes to GPIO0_001F_IRQn (pins 0-31). */
#define TIKU_GPIO_IRQ_MAX_PAD  31u

/*---------------------------------------------------------------------------*/
/* Helpers                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Convert a (port, pin) pair to a pad index, or -1 if out of range. */
static int pad_of(uint8_t port, uint8_t pin, uint32_t *pad) {
    uint32_t p;
    if (port < 1u || pin > 7u) {
        return -1;
    }
    p = (uint32_t)(port - 1u) * 8u + pin;
    if (p >= 128u) {
        return -1;
    }
    *pad = p;
    return 0;
}

/** @brief Map a platform-agnostic edge selector to an eIntDir value. */
static uint32_t edge_to_intdir(tiku_gpio_edge_t edge) {
    switch (edge) {
    case TIKU_GPIO_EDGE_RISING:  return TIKU_INTDIR_LO2HI;
    case TIKU_GPIO_EDGE_FALLING: return TIKU_INTDIR_HI2LO;
    case TIKU_GPIO_EDGE_BOTH:    return TIKU_INTDIR_BOTH;
    default:                     return TIKU_INTDIR_NONE;
    }
}

/** @brief Write a pad configuration register under the PADKEY lock. */
static void pad_config(uint32_t pad, uint32_t cfg) {
    GPIO->PADKEY = TIKU_GPIO_PADKEY_UNLOCK;
    (&GPIO->PINCFG0)[pad] = cfg;
    GPIO->PADKEY = 0u;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Enable an edge-triggered interrupt on (port, pin).
 *
 * Configures the pad as a GPIO input with the requested edge in eIntDir,
 * clears any stale latch, unmasks the pad in GPIO->MCUN0INT0EN, and enables
 * GPIO0_001F_IRQn in the NVIC. Pads above 31 have no vector wired here.
 */
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin,
                              tiku_gpio_edge_t edge) {
    uint32_t pad, intdir;

    if (pad_of(port, pin, &pad)) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    if (pad > TIKU_GPIO_IRQ_MAX_PAD) {
        return TIKU_GPIO_IRQ_ERR_UNSUP;   /* only GPIO0_001F (IRQ 56) is wired */
    }
    intdir = edge_to_intdir(edge);
    if (intdir == TIKU_INTDIR_NONE) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }

    /* Input + edge select. */
    pad_config(pad, TIKU_GPIO_FNCSEL_GPIO | TIKU_GPIO_INPEN |
                    (intdir << TIKU_GPIO_INTDIR_POS));

    /* Clear any stale edge, then unmask this pad to GPIO0_001F. */
    GPIO->MCUN0INT0CLR = (1u << pad);
    GPIO->MCUN0INT0EN |= (1u << pad);

    NVIC_ClearPendingIRQ(GPIO0_001F_IRQn);
    NVIC_EnableIRQ(GPIO0_001F_IRQn);
    return TIKU_GPIO_IRQ_OK;
}

/**
 * @brief Mask the interrupt for (port, pin) and clear any pending latch.
 *
 * The pad is left a plain GPIO input (eIntDir cleared) so the line can still
 * be read; the NVIC vector stays enabled for any other armed pads.
 */
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin) {
    uint32_t pad;

    if (pad_of(port, pin, &pad) || pad > TIKU_GPIO_IRQ_MAX_PAD) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    GPIO->MCUN0INT0EN &= ~(1u << pad);
    GPIO->MCUN0INT0CLR = (1u << pad);
    pad_config(pad, TIKU_GPIO_FNCSEL_GPIO | TIKU_GPIO_INPEN);  /* drop eIntDir */
    return TIKU_GPIO_IRQ_OK;
}

/*---------------------------------------------------------------------------*/
/* IRQ handler -- strong override of the weak crt_early vector slot (IRQ 56)  */
/*---------------------------------------------------------------------------*/

/**
 * @brief GPIO0 pins 0-31 interrupt service routine.
 *
 * Reads the latched edges from MCUN0INT0STAT, clears them, and broadcasts one
 * TIKU_EVENT_GPIO per fired pad (data = TIKU_GPIO_IRQ_PACK(port, pin)).
 */
void tiku_ambiq_gpio0_isr(void) {
    uint32_t stat = GPIO->MCUN0INT0STAT;
    uint32_t pad;

    GPIO->MCUN0INT0CLR = stat;   /* clear all serviced edges up front */

    for (pad = 0u; pad <= TIKU_GPIO_IRQ_MAX_PAD; pad++) {
        if (stat & (1u << pad)) {
            uint8_t port = (uint8_t)((pad / 8u) + 1u);
            uint8_t pin  = (uint8_t)(pad % 8u);
            tiku_event_data_t data =
                (tiku_event_data_t)TIKU_GPIO_IRQ_PACK(port, pin);
            tiku_process_post(TIKU_PROCESS_BROADCAST, TIKU_EVENT_GPIO, data);
        }
    }
}
