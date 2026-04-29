/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_hal.h - Platform-agnostic GPIO interrupt interface
 *
 * Bridges per-pin edge interrupts on the underlying MCU into
 * TIKU_EVENT_GPIO process events. The arch backend owns the
 * register-level configuration (edge-select, IE, IFG) and the
 * ISR; the kernel-side abstraction is just a single API plus a
 * single event identifier.
 *
 * Event payload encoding (passed via tiku_event_data_t):
 *
 *     ((port & 0xFF) << 8) | (pin & 0xFF)
 *
 * Example: P1.3 fires => data = 0x0103. The macros below extract
 * the fields without the caller having to remember the layout.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_GPIO_IRQ_HAL_H_
#define TIKU_GPIO_IRQ_HAL_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* EDGE-SELECT TYPE                                                          */
/*---------------------------------------------------------------------------*/

typedef enum {
    TIKU_GPIO_EDGE_RISING  = 0,  /**< Trigger on low->high transitions */
    TIKU_GPIO_EDGE_FALLING = 1,  /**< Trigger on high->low transitions */
    TIKU_GPIO_EDGE_BOTH    = 2,  /**< Trigger on either edge (toggle IES on each fire) */
} tiku_gpio_edge_t;

/*---------------------------------------------------------------------------*/
/* RETURN CODES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_GPIO_IRQ_OK            0
#define TIKU_GPIO_IRQ_ERR_INVALID  -1   /**< Bad port/pin/edge */
#define TIKU_GPIO_IRQ_ERR_UNSUP    -2   /**< Port has no IRQ vector on this device */

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
 * Configures the pin as an input with the platform's standard
 * pull resistor enabled, sets the edge-select bit per @p edge,
 * clears any pending flag, and unmasks the interrupt. Subsequent
 * matching edges post a TIKU_EVENT_GPIO broadcast event whose
 * data field is TIKU_GPIO_IRQ_PACK(port, pin).
 *
 * @return TIKU_GPIO_IRQ_OK or a negative error code.
 */
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin,
                              tiku_gpio_edge_t edge);

/**
 * @brief Mask the interrupt and clear any pending flag.
 *
 * Pin direction and pull state are left unchanged so the
 * application can read the line via tiku_gpio_read() afterwards
 * if desired.
 */
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin);

#endif /* TIKU_GPIO_IRQ_HAL_H_ */
