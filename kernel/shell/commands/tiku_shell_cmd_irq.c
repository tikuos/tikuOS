/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_irq.c - "irq" command implementation
 *
 * Thin parser layer over the GPIO IRQ HAL. Accepts a pin in
 * "P<port>.<pin>" form and one of the four edge keywords.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_irq.h"
#include <kernel/shell/tiku_shell.h>
#include <interfaces/gpio/tiku_gpio.h>

/**
 * @brief Compare two NUL-terminated strings for equality.
 */
static uint8_t
streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == *b);
}

/* Parse "P<port>.<pin>" -> port and pin. Returns 0 on success,
 * -1 on syntax error. Accepts uppercase or lowercase 'p'.  The pin field
 * is one or two decimal digits (0..31) so wide ports such as the nRF54L's
 * P1 (up to P1.15) are addressable, not just the 0..7 of narrow parts. */
static int
parse_pin_spec(const char *s, uint8_t *port, uint8_t *pin)
{
    unsigned v;

    if (s == NULL) {
        return -1;
    }
    if (*s != 'P' && *s != 'p') {
        return -1;
    }
    s++;
    if (*s < '0' || *s > '9') {
        return -1;
    }
    *port = (uint8_t)(*s - '0');
    s++;
    if (*s != '.') {
        return -1;
    }
    s++;
    if (*s < '0' || *s > '9') {
        return -1;
    }
    v = (unsigned)(*s - '0');
    s++;
    if (*s >= '0' && *s <= '9') {
        v = (v * 10u) + (unsigned)(*s - '0');
        s++;
    }
    if (*s != '\0' || v > 31u) {
        return -1;
    }
    *pin = (uint8_t)v;
    return 0;
}

void
tiku_shell_cmd_irq(uint8_t argc, const char *argv[])
{
    uint8_t port, pin, vport;
    tiku_gpio_edge_t edge;
    int rc;

    if (argc < 3) {
        SHELL_PRINTF("Usage: irq P<port>.<pin> <rising|falling|both|off>\n");
        return;
    }

    if (parse_pin_spec(argv[1], &port, &pin) != 0) {
        SHELL_PRINTF("irq: bad pin spec '%s' (expected like 'P1.3')\n",
                     argv[1]);
        return;
    }

    /* The GPIO-IRQ arch API is 1-based virtual (1 = the device's first port).
     * The user types the physical port NAME: nRF54L parts are named from P0,
     * so P0/P1/P2 map to virtual 1/2/3 (matching tiku_gpio_arch.c); parts named
     * from P1 (MSP430) already have name == virtual port. */
#if defined(PLATFORM_NORDIC)
    vport = (uint8_t)(port + 1u);
#else
    vport = port;
#endif

    if (streq(argv[2], "off")) {
        rc = tiku_gpio_irq_disable(vport, pin);
        if (rc == TIKU_GPIO_IRQ_OK) {
            SHELL_PRINTF("Disabled: P%u.%u\n",
                         (unsigned)port, (unsigned)pin);
        } else {
            SHELL_PRINTF("irq: disable failed (%d)\n", rc);
        }
        return;
    }

    if      (streq(argv[2], "rising"))  edge = TIKU_GPIO_EDGE_RISING;
    else if (streq(argv[2], "falling")) edge = TIKU_GPIO_EDGE_FALLING;
    else if (streq(argv[2], "both"))    edge = TIKU_GPIO_EDGE_BOTH;
    else {
        SHELL_PRINTF("irq: unknown edge '%s'\n", argv[2]);
        return;
    }

    rc = tiku_gpio_irq_enable(vport, pin, edge);
    if (rc == TIKU_GPIO_IRQ_OK) {
        SHELL_PRINTF("Enabled: P%u.%u %s edge -> event\n",
                     (unsigned)port, (unsigned)pin, argv[2]);
    } else if (rc == TIKU_GPIO_IRQ_ERR_UNSUP) {
        SHELL_PRINTF("irq: port P%u has no IRQ on this device\n",
                     (unsigned)port);
    } else {
        SHELL_PRINTF("irq: enable failed (%d)\n", rc);
    }
}
