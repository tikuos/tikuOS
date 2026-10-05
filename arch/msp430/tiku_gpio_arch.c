/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - MSP430 GPIO port access implementation
 *
 * Maps device-declared port numbers (1-9, J) to register addresses at runtime.
 * All functions validate port/pin before touching registers.
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
#if TIKU_DEVICE_HAS_PORT5
        [5] = { &P5IN, &P5OUT, &P5DIR, &P5REN },
#endif
#if TIKU_DEVICE_HAS_PORT6
        [6] = { &P6IN, &P6OUT, &P6DIR, &P6REN },
#endif
#if TIKU_DEVICE_HAS_PORT7
        [7] = { &P7IN, &P7OUT, &P7DIR, &P7REN },
#endif
#if TIKU_DEVICE_HAS_PORT8
        [8] = { &P8IN, &P8OUT, &P8DIR, &P8REN },
#endif
#if TIKU_DEVICE_HAS_PORT9
        [9] = { &P9IN, &P9OUT, &P9DIR, &P9REN },
#endif
    };

#if TIKU_DEVICE_HAS_PORTJ
    /* Port J registers are declared as 16-bit on MSP430 (PJ is shared
     * with JTAG), but the gpio_port_t struct stores the byte-wide
     * register pointer because the upper byte is reserved.  Cast to
     * silence the incompatible-pointer-type warning; on little-endian
     * MSP430 the byte access at &PJxN reads/writes the 8 GPIO bits. */
    static const gpio_port_t portj = {
        (volatile uint8_t *)&PJIN,
        (volatile uint8_t *)&PJOUT,
        (volatile uint8_t *)&PJDIR,
        (volatile uint8_t *)&PJREN
    };
    if (port == 0xFF) {
        return &portj;
    }
#endif

    if (port == 0 || port >= sizeof(ports) / sizeof(ports[0])) {
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

/**
 * @brief Report whether a pin is routed to a module function.
 *
 * A set PxSEL0 or PxSEL1 bit hands the pin to a peripheral; the registers are
 * only read.  LCD_C segment pins are selected through LCDCPCTLx instead, so
 * they read as GPIO here.
 *
 * @return 1 when routed to a peripheral, 0 for a GPIO, -1 if port/pin invalid
 */
int
tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin)
{
    unsigned int sel;

    if (gpio_validate(port, pin) == (const gpio_port_t *)0) {
        return -1;
    }
    switch (port) {
#if TIKU_DEVICE_HAS_PORT1
    case 1:
        sel = P1SEL0 | P1SEL1;
        break;
#endif
#if TIKU_DEVICE_HAS_PORT2
    case 2:
        sel = P2SEL0 | P2SEL1;
        break;
#endif
#if TIKU_DEVICE_HAS_PORT3
    case 3:
        sel = P3SEL0 | P3SEL1;
        break;
#endif
#if TIKU_DEVICE_HAS_PORT4
    case 4:
        sel = P4SEL0 | P4SEL1;
        break;
#endif
#if TIKU_DEVICE_HAS_PORT5
    case 5:
        sel = P5SEL0 | P5SEL1;
        break;
#endif
#if TIKU_DEVICE_HAS_PORT6
    case 6:
        sel = P6SEL0 | P6SEL1;
        break;
#endif
#if TIKU_DEVICE_HAS_PORT7
    case 7:
        sel = P7SEL0 | P7SEL1;
        break;
#endif
#if TIKU_DEVICE_HAS_PORT8
    case 8:
        sel = P8SEL0 | P8SEL1;
        break;
#endif
#if TIKU_DEVICE_HAS_PORT9
    case 9:
        sel = P9SEL0 | P9SEL1;
        break;
#endif
#if TIKU_DEVICE_HAS_PORTJ
    case 0xFF:
        sel = PJSEL0 | PJSEL1;
        break;
#endif
    default:
        return -1;
    }
    return (int)((sel >> pin) & 1u);
}
