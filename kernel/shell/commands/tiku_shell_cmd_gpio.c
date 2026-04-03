/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_gpio.c - "gpio" command implementation
 *
 * Direct GPIO pin control from the shell.  Reads, writes, toggles,
 * or configures any port/pin through the arch GPIO layer.
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

#include "tiku_shell_cmd_gpio.h"
#include <kernel/shell/tiku_shell.h>
#include <arch/msp430/tiku_gpio_arch.h>

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** Parse a single decimal digit (0-9), or 0xFF on error */
static uint8_t
parse_digit(const char *s)
{
    if (s[0] >= '0' && s[0] <= '9' && s[1] == '\0') {
        return (uint8_t)(s[0] - '0');
    }
    return 0xFF;
}

/** Parse port number: 1-4 or 'J'/'j' (mapped to 0xFF for arch layer) */
static uint8_t
parse_port(const char *s)
{
    if ((s[0] == 'J' || s[0] == 'j') && s[1] == '\0') {
        return 0xFF;
    }
    return parse_digit(s);
}

/** Format port number for display */
static char
port_char(uint8_t port)
{
    if (port == 0xFF) {
        return 'J';
    }
    return '0' + port;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_gpio(uint8_t argc, const char *argv[])
{
    uint8_t port;
    uint8_t pin;
    int8_t val;

    if (argc < 3) {
        SHELL_PRINTF("Usage: gpio <port> <pin> [0|1|t|in]\n");
        SHELL_PRINTF("  port: 1-4 or J    pin: 0-7\n");
        return;
    }

    port = parse_port(argv[1]);
    pin = parse_digit(argv[2]);

    if (port == 0 || pin == 0xFF) {
        SHELL_PRINTF("Error: invalid port/pin\n");
        return;
    }

    /* No value argument — read the pin */
    if (argc < 4) {
        val = tiku_gpio_arch_read(port, pin);
        if (val < 0) {
            SHELL_PRINTF("Error: P%c.%u not available\n",
                         port_char(port), pin);
            return;
        }
        {
            int8_t dir = tiku_gpio_arch_get_dir(port, pin);
            SHELL_PRINTF("P%c.%u = %u (%s)\n",
                         port_char(port), pin, (uint8_t)val,
                         dir ? "output" : "input");
        }
        return;
    }

    /* Value argument — write, toggle, or set input */
    if (argv[3][0] == '1' && argv[3][1] == '\0') {
        if (tiku_gpio_arch_write(port, pin, 1) < 0) {
            SHELL_PRINTF("Error: P%c.%u not available\n",
                         port_char(port), pin);
            return;
        }
        SHELL_PRINTF("P%c.%u -> 1\n", port_char(port), pin);

    } else if (argv[3][0] == '0' && argv[3][1] == '\0') {
        if (tiku_gpio_arch_write(port, pin, 0) < 0) {
            SHELL_PRINTF("Error: P%c.%u not available\n",
                         port_char(port), pin);
            return;
        }
        SHELL_PRINTF("P%c.%u -> 0\n", port_char(port), pin);

    } else if (argv[3][0] == 't' && argv[3][1] == '\0') {
        if (tiku_gpio_arch_toggle(port, pin) < 0) {
            SHELL_PRINTF("Error: P%c.%u not available\n",
                         port_char(port), pin);
            return;
        }
        val = tiku_gpio_arch_read(port, pin);
        SHELL_PRINTF("P%c.%u -> %u\n", port_char(port), pin,
                     (uint8_t)val);

    } else if (argv[3][0] == 'i' && argv[3][1] == 'n') {
        if (tiku_gpio_arch_set_input(port, pin) < 0) {
            SHELL_PRINTF("Error: P%c.%u not available\n",
                         port_char(port), pin);
            return;
        }
        SHELL_PRINTF("P%c.%u -> input (pull-up)\n",
                     port_char(port), pin);

    } else {
        SHELL_PRINTF("Error: value must be 0, 1, t, or in\n");
    }
}
