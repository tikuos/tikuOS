/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_apollo4l.c - Apollo4 Lite GPIO edge interrupts.
 *
 * Turns pin edges into TIKU_EVENT_GPIO broadcasts.  Only pads 0-31 are
 * handled, on GPIO0_001F (IRQ 56); enabling a higher pad returns
 * TIKU_GPIO_IRQ_ERR_UNSUP.  The edge select is PINCFG IRPTEN, under PADKEY.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>
#include "apollo4l.h"            /* GPIO struct, NVIC, GPIO0_001F_IRQn */
#include <kernel/process/tiku_process.h>
#include <kernel/process/tiku_proto.h>
#include <kernel/vfs/tree/tiku_vfs_tree_gpio.h>   /* edge notify from ISR */
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* PINCFG FIELDS                                                             */
/*---------------------------------------------------------------------------*/

#define TIKU_GPIO_FNCSEL_GPIO    3u           /**< FNCSEL[3:0] = GPIO        */
#define TIKU_GPIO_INPEN          (1u << 4)    /**< INPEN[4] input enable     */
#define TIKU_GPIO_INTDIR_POS     6u           /**< IRPTEN[7:6] edge select   */
#define TIKU_GPIO_PADKEY_UNLOCK  0x73u        /**< GPIO_PADKEY unlock value  */

/** IRPTEN encodings (GPIO_PINCFG0_IRPTEN0_* in apollo4l.h). */
#define TIKU_INTDIR_NONE   0u
#define TIKU_INTDIR_HI2LO  1u   /**< high->low (falling) */
#define TIKU_INTDIR_LO2HI  2u   /**< low->high (rising)  */
#define TIKU_INTDIR_BOTH   3u

/** Highest pad that routes to GPIO0_001F_IRQn (pins 0-31). */
#define TIKU_GPIO_IRQ_MAX_PAD  31u

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Store the pad of (port, pin) in *pad and return 0; -1 if invalid. */
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

/** @brief Map a platform-agnostic edge selector to an IRPTEN value. */
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
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Enable an edge-triggered interrupt on (port, pin).
 *
 * Configures the pad as a GPIO input with the requested edge in IRPTEN,
 * clears any stale latch, unmasks the pad in GPIO->MCUN0INT0EN, and enables
 * GPIO0_001F_IRQn in the NVIC.
 *
 * @return TIKU_GPIO_IRQ_OK, TIKU_GPIO_IRQ_ERR_INVALID for a bad pad or edge,
 *         or TIKU_GPIO_IRQ_ERR_UNSUP for a pad above 31
 */
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin,
                              tiku_gpio_edge_t edge) {
    uint32_t pad, intdir;

    if (pad_of(port, pin, &pad)) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    if (pad > TIKU_GPIO_IRQ_MAX_PAD) {
        return TIKU_GPIO_IRQ_ERR_UNSUP;   /* only GPIO0_001F is handled */
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
 * The pad is left a plain GPIO input (IRPTEN cleared) so the line can still
 * be read; the NVIC vector stays enabled for any other armed pads.  A pad
 * above 31 returns TIKU_GPIO_IRQ_ERR_INVALID.
 */
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin) {
    uint32_t pad;

    if (pad_of(port, pin, &pad) || pad > TIKU_GPIO_IRQ_MAX_PAD) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    GPIO->MCUN0INT0EN &= ~(1u << pad);
    GPIO->MCUN0INT0CLR = (1u << pad);
    pad_config(pad, TIKU_GPIO_FNCSEL_GPIO | TIKU_GPIO_INPEN);  /* IRPTEN 0 */
    return TIKU_GPIO_IRQ_OK;
}

/*---------------------------------------------------------------------------*/
/* IRQ HANDLER                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief GPIO0 pins 0-31 interrupt service routine (IRQ 56).
 *
 * Reads the latched edges from MCUN0INT0STAT, clears them, and broadcasts one
 * TIKU_EVENT_GPIO per fired pad (data = TIKU_GPIO_IRQ_PACK(port, pin)).
 * Overrides the weak alias in tiku_crt_early_apollo4l.c.
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
            /* Notify VFS watchers of /dev/gpio/<port>/<pin>; ISR-safe. */
            tiku_vfs_tree_gpio_notify(port, pin);
        }
    }
}
