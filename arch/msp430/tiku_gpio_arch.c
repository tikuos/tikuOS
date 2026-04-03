/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - MSP430 GPIO port access implementation
 *
 * Maps port numbers (1-4, J) to MSP430 register addresses at runtime.
 * All functions validate port/pin before touching registers.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_gpio_arch.h"
#include "tiku_device_select.h"
#include <msp430.h>

/*---------------------------------------------------------------------------*/
/* PORT REGISTER MAPPING                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Descriptor for one 8-bit GPIO port's registers.
 */
typedef struct {
    volatile uint8_t *in;
    volatile uint8_t *out;
    volatile uint8_t *dir;
    volatile uint8_t *ren;
} gpio_port_t;

/**
 * @brief Look up register pointers for a port number.
 *
 * Port J is mapped as port number 0xFF (special case).
 * Returns NULL if the port is not available on this device.
 */
static const gpio_port_t *
gpio_get_port(uint8_t port)
{
    /* Static table — populated only for ports this device has */
    static const gpio_port_t ports[] = {
#if TIKU_DEVICE_HAS_PORT1
        [1] = { &P1IN, &P1OUT, &P1DIR, &P1REN },
#endif
#if TIKU_DEVICE_HAS_PORT2
        [2] = { &P2IN, &P2OUT, &P2DIR, &P2REN },
#endif
#if TIKU_DEVICE_HAS_PORT3
        [3] = { &P3IN, &P3OUT, &P3DIR, &P3REN },
#endif
#if TIKU_DEVICE_HAS_PORT4
        [4] = { &P4IN, &P4OUT, &P4DIR, &P4REN },
#endif
    };

#if TIKU_DEVICE_HAS_PORTJ
    static const gpio_port_t portj = {
        &PJIN, &PJOUT, &PJDIR, &PJREN
    };
    if (port == 0xFF) {
        return &portj;
    }
#endif

    if (port == 0 || port > 4) {
        return (const gpio_port_t *)0;
    }

    /* Check if this port has registers (in ptr is non-NULL) */
    if (ports[port].in == (volatile uint8_t *)0) {
        return (const gpio_port_t *)0;
    }

    return &ports[port];
}

/**
 * @brief Validate port and pin, return port descriptor.
 */
static const gpio_port_t *
gpio_validate(uint8_t port, uint8_t pin)
{
    if (pin > 7) {
        return (const gpio_port_t *)0;
    }
    return gpio_get_port(port);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

int8_t
tiku_gpio_arch_set_output(uint8_t port, uint8_t pin)
{
    const gpio_port_t *p = gpio_validate(port, pin);
    if (p == (const gpio_port_t *)0) {
        return -1;
    }
    *p->dir |= (1 << pin);
    return 0;
}

int8_t
tiku_gpio_arch_set_input(uint8_t port, uint8_t pin)
{
    const gpio_port_t *p = gpio_validate(port, pin);
    if (p == (const gpio_port_t *)0) {
        return -1;
    }
    *p->dir &= ~(1 << pin);
    *p->ren |= (1 << pin);       /* Enable pull resistor */
    *p->out |= (1 << pin);       /* Pull-up (not pull-down) */
    return 0;
}

int8_t
tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val)
{
    const gpio_port_t *p = gpio_validate(port, pin);
    if (p == (const gpio_port_t *)0) {
        return -1;
    }
    *p->dir |= (1 << pin);       /* Ensure output */
    if (val) {
        *p->out |= (1 << pin);
    } else {
        *p->out &= ~(1 << pin);
    }
    return 0;
}

int8_t
tiku_gpio_arch_toggle(uint8_t port, uint8_t pin)
{
    const gpio_port_t *p = gpio_validate(port, pin);
    if (p == (const gpio_port_t *)0) {
        return -1;
    }
    *p->dir |= (1 << pin);       /* Ensure output */
    *p->out ^= (1 << pin);
    return 0;
}

int8_t
tiku_gpio_arch_read(uint8_t port, uint8_t pin)
{
    const gpio_port_t *p = gpio_validate(port, pin);
    if (p == (const gpio_port_t *)0) {
        return -1;
    }
    return (*p->in >> pin) & 1;
}

int8_t
tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin)
{
    const gpio_port_t *p = gpio_validate(port, pin);
    if (p == (const gpio_port_t *)0) {
        return -1;
    }
    return (*p->dir >> pin) & 1;
}
