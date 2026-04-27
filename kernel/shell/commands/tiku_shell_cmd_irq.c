/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_irq.c - "irq" command implementation
 *
 * Thin parser layer over the GPIO IRQ HAL. Accepts a pin in
 * "P<port>.<pin>" form and one of the four edge keywords.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_irq.h"
#include <kernel/shell/tiku_shell.h>
#include <interfaces/gpio/tiku_gpio.h>

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
 * -1 on syntax error. Accepts uppercase or lowercase 'p'. */
static int
parse_pin_spec(const char *s, uint8_t *port, uint8_t *pin)
{
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
    if (*s < '0' || *s > '7' || s[1] != '\0') {
        return -1;
    }
    *pin = (uint8_t)(*s - '0');
    return 0;
}

void
tiku_shell_cmd_irq(uint8_t argc, const char *argv[])
{
    uint8_t port, pin;
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

    if (streq(argv[2], "off")) {
        rc = tiku_gpio_irq_disable(port, pin);
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

    rc = tiku_gpio_irq_enable(port, pin, edge);
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
