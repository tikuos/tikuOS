/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_hal.h - platform-agnostic GPIO interrupt interface.
 *
 * Declares the per-port edge-interrupt calls and the TIKU_EVENT_GPIO payload
 * macros.  The port owns edge selection, the enable and pending flags and the
 * ISR, which broadcasts TIKU_EVENT_GPIO with port and pin packed in the data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_GPIO_IRQ_HAL_H_
#define TIKU_GPIO_IRQ_HAL_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* EDGE-SELECT TYPE                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Which edge of a pin raises its interrupt. */
typedef enum {
    TIKU_GPIO_EDGE_RISING  = 0,  /**< Trigger on low->high transitions */
    TIKU_GPIO_EDGE_FALLING = 1,  /**< Trigger on high->low transitions */
    TIKU_GPIO_EDGE_BOTH    = 2,  /**< Trigger on either edge */
} tiku_gpio_edge_t;

/*---------------------------------------------------------------------------*/
/* RETURN CODES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_GPIO_IRQ_OK            0   /**< Success */
#define TIKU_GPIO_IRQ_ERR_INVALID  -1   /**< Bad port/pin/edge */
#define TIKU_GPIO_IRQ_ERR_UNSUP    -2   /**< No interrupt for this pin here */

/*---------------------------------------------------------------------------*/
/* EVENT PAYLOAD HELPERS                                                     */
/*---------------------------------------------------------------------------*/

/** Pack port+pin into the event data word. */
#define TIKU_GPIO_IRQ_PACK(port, pin)  \
    ((uintptr_t)(((unsigned)(port) << 8) | ((unsigned)(pin) & 0xFFu)))

/** Extract the port number from a TIKU_EVENT_GPIO data word. */
#define TIKU_GPIO_IRQ_PORT(data)  \
    ((uint8_t)(((uintptr_t)(data) >> 8) & 0xFFu))

/** Extract the pin number from a TIKU_EVENT_GPIO data word. */
#define TIKU_GPIO_IRQ_PIN(data)  \
    ((uint8_t)((uintptr_t)(data) & 0xFFu))

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Enable an edge-triggered interrupt on the given pin.
 *
 * Makes the pin an input (pulled up on MSP430, RP2350 and nRF54L), sets the
 * edge, clears any pending flag and unmasks.  Each matching edge broadcasts
 * TIKU_EVENT_GPIO with data TIKU_GPIO_IRQ_PACK(port, pin).
 *
 * @param port  GPIO port
 * @param pin   Pin within the port
 * @param edge  Edge that raises the interrupt
 * @return TIKU_GPIO_IRQ_OK or a negative error code.
 */
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin,
                              tiku_gpio_edge_t edge);

/**
 * @brief Mask the interrupt and clear any pending flag.
 *
 * Leaves pin direction and pull unchanged; tiku_gpio_read() reads the line
 * afterwards.
 *
 * @param port  GPIO port
 * @param pin   Pin within the port
 * @return TIKU_GPIO_IRQ_OK or a negative error code.
 */
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin);

#endif /* TIKU_GPIO_IRQ_HAL_H_ */
