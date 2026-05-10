/*
 * Tiku Operating System v0.04
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

#define MAX_GP_PIN  29U

static uint8_t edge_to_inte_bits(tiku_gpio_edge_t edge) {
    switch (edge) {
    case TIKU_GPIO_EDGE_RISING:  return RP2350_IO_INT_EDGE_HIGH;
    case TIKU_GPIO_EDGE_FALLING: return RP2350_IO_INT_EDGE_LOW;
    case TIKU_GPIO_EDGE_BOTH:    return RP2350_IO_INT_EDGE_HIGH | RP2350_IO_INT_EDGE_LOW;
    default:                     return 0U;
    }
}

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
