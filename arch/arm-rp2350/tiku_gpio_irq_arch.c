/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.c - RP2350 GPIO interrupt backend
 *
 * Maps the platform-agnostic (port, pin, edge) request onto bank-0
 * IO interrupt registers. INTR/INTE/INTS arrays are 4 entries long
 * (8 pins per word). Each pin gets a 4-bit field with separate
 * level-low / level-high / edge-low / edge-high mask bits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>
#include "tiku_rp2350_regs.h"
#include "tiku_gpio_arch.h"
#include <kernel/process/tiku_process.h>
#include <kernel/process/tiku_proto.h>
#include <stdint.h>

/** Maximum zero-based GP index on the RP2350 (GP0–GP29). */
#define MAX_GP_PIN  29U

/**
 * @brief Convert a platform-agnostic edge selector to RP2350 INTE nibble bits.
 *
 * Maps @c TIKU_GPIO_EDGE_RISING, @c TIKU_GPIO_EDGE_FALLING, and
 * @c TIKU_GPIO_EDGE_BOTH to the corresponding @c RP2350_IO_INT_EDGE_*
 * bit masks that are written into an IO_BANK0 INTE register nibble.
 *
 * @param edge  Edge polarity selector (@c tiku_gpio_edge_t).
 * @return      Four-bit INTE mask for the requested edge(s), or 0 for an
 *              unrecognised value.
 */
static uint8_t edge_to_inte_bits(tiku_gpio_edge_t edge) {
    switch (edge) {
    case TIKU_GPIO_EDGE_RISING:  return RP2350_IO_INT_EDGE_HIGH;
    case TIKU_GPIO_EDGE_FALLING: return RP2350_IO_INT_EDGE_LOW;
    case TIKU_GPIO_EDGE_BOTH:    return RP2350_IO_INT_EDGE_HIGH | RP2350_IO_INT_EDGE_LOW;
    default:                     return 0U;
    }
}

/**
 * @brief Compute the flat GP index from a virtual (port, pin) pair.
 *
 * TikuOS addresses GPIO through a (port 1–4, pin 0–7) abstraction.
 * This function converts that pair to the RP2350 zero-based GP number
 * (GP0–GP29) used by the IO_BANK0 register arrays.
 *
 * @param port  Virtual port number (1–4).
 * @param pin   Pin index within the port (0–7).
 * @return      Zero-based GP index (0–29) on success, or -1 when the
 *              (port, pin) combination is out of range.
 */
static int8_t gp_index(uint8_t port, uint8_t pin) {
    if (port < 1U || port > 4U || pin > 7U) {
        return -1;
    }
    uint8_t idx = (uint8_t)(((port - 1U) * 8U) + pin);
    if (idx > MAX_GP_PIN) {
        return -1;
    }
    return (int8_t)idx;
}

/**
 * @brief Enable a GPIO edge interrupt for the given (port, pin, edge).
 *
 * Translates the virtual (port, pin) pair to a GP index, configures the
 * corresponding nibble in the IO_BANK0 PROC0_INTE register, clears any
 * stale pending edge in INTR, and unmasks IO_BANK0 in the NVIC.  The pin
 * is also reconfigured as an SIO input with pull-up.
 *
 * @param port  Virtual port number (1–4).
 * @param pin   Pin index within the port (0–7).
 * @param edge  Edge polarity to arm (@c TIKU_GPIO_EDGE_RISING,
 *              @c TIKU_GPIO_EDGE_FALLING, or @c TIKU_GPIO_EDGE_BOTH).
 * @return      @c TIKU_GPIO_IRQ_OK on success, or
 *              @c TIKU_GPIO_IRQ_ERR_INVALID when the arguments are
 *              out of range or @p edge is unrecognised.
 */
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin,
                              tiku_gpio_edge_t edge) {
    int8_t gp = gp_index(port, pin);
    if (gp < 0) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    uint8_t bits = edge_to_inte_bits(edge);
    if (bits == 0U) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }

    /* Make sure the pin is an SIO input with pull-up. */
    (void)tiku_gpio_arch_set_input(port, pin);

    /* Locate the INTE word + nibble. */
    uint8_t  word  = (uint8_t)(gp >> 3);          /* gp / 8 */
    uint8_t  shift = (uint8_t)((gp & 7U) << 2);   /* (gp % 8) * 4 */
    uint32_t mask  = (uint32_t)bits << shift;

    /* Clear any pending edge from before we armed. */
    _RP2350_REG_SET(RP2350_IO_BANK0_INTR(word), mask);

    /* Unmask the edge in PROC0_INTE. */
    _RP2350_REG_SET(RP2350_IO_BANK0_PROC0_INTE(word), mask);

    /* Make sure IO_BANK0 is unmasked in the NVIC. */
    rp2350_nvic_clear_pending(RP2350_IRQ_IO_BANK0);
    rp2350_nvic_enable(RP2350_IRQ_IO_BANK0);

    return TIKU_GPIO_IRQ_OK;
}

/**
 * @brief Disable the GPIO edge interrupt for the given (port, pin).
 *
 * Clears all four edge/level bits for the pin's nibble in the IO_BANK0
 * PROC0_INTE register and then clears any latched pending edge in INTR.
 * The NVIC mask for IO_BANK0 is left unchanged; use the HAL-level disable
 * if no other pins on the bank require interrupts.
 *
 * @param port  Virtual port number (1–4).
 * @param pin   Pin index within the port (0–7).
 * @return      @c TIKU_GPIO_IRQ_OK on success, or
 *              @c TIKU_GPIO_IRQ_ERR_INVALID when (port, pin) is out of range.
 */
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin) {
    int8_t gp = gp_index(port, pin);
    if (gp < 0) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    uint8_t  word  = (uint8_t)(gp >> 3);
    uint8_t  shift = (uint8_t)((gp & 7U) << 2);
    uint32_t mask  = 0xFU << shift;   /* clear all four edge/level bits */

    _RP2350_REG_CLR(RP2350_IO_BANK0_PROC0_INTE(word), mask);
    /* Clear any pending. */
    _RP2350_REG_SET(RP2350_IO_BANK0_INTR(word), mask);
    return TIKU_GPIO_IRQ_OK;
}

/*---------------------------------------------------------------------------*/
/* IRQ handler                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief IO_BANK0 interrupt service routine (ISR context).
 *
 * Called by the NVIC when at least one IO_BANK0 edge has fired.  Walks
 * all four PROC0_INTS words, decodes every armed nibble into a virtual
 * (port, pin) pair, broadcasts @c TIKU_EVENT_GPIO (data packed by
 * @c TIKU_GPIO_IRQ_PACK) to all registered processes, and clears the
 * latched edge in INTR before returning.
 */
void tiku_rp2350_io_bank0_isr(void) {
    /* Walk the four INTS words. Each set 4-bit nibble identifies a
     * pin that has fired; compose the (port, pin) tuple per the
     * virtual-port mapping and post one TIKU_EVENT_GPIO event. */
    uint8_t word;
    for (word = 0U; word < 4U; word++) {
        uint32_t ints = _RP2350_REG(RP2350_IO_BANK0_PROC0_INTS(word));
        if (ints == 0U) {
            continue;
        }
        uint8_t nibble;
        for (nibble = 0U; nibble < 8U; nibble++) {
            uint8_t bits = (uint8_t)((ints >> (nibble * 4U)) & 0xFU);
            if (bits == 0U) {
                continue;
            }
            uint8_t gp = (uint8_t)((word * 8U) + nibble);
            if (gp > MAX_GP_PIN) {
                continue;
            }
            uint8_t port = (uint8_t)((gp / 8U) + 1U);
            uint8_t pin  = (uint8_t)(gp % 8U);

            tiku_event_data_t data = (tiku_event_data_t)
                TIKU_GPIO_IRQ_PACK(port, pin);
            tiku_process_post(TIKU_PROCESS_BROADCAST,
                              TIKU_EVENT_GPIO, data);

            /* Clear the latched edge by writing the same nibble to
             * INTR. */
            _RP2350_REG_SET(RP2350_IO_BANK0_INTR(word),
                            (uint32_t)bits << (nibble * 4U));
        }
    }
}
