/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_gpio.c - "gpio" command implementation
 *
 * Direct GPIO pin control from the shell.  Reads, writes, toggles, or
 * configures any port/pin in the platform geometry through the arch GPIO
 * layer, and refuses to change a pin that has an owner.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_gpio.h"
#include <kernel/shell/tiku_shell.h>
#include <interfaces/gpio/tiku_gpio.h>
#include <interfaces/gpio/tiku_gpio_geometry.h>
#include <interfaces/gpio/tiku_gpio_owner.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Port number the GPIO interface uses for MSP430 port J. */
#define GPIO_PORT_J         255

/** @brief Most digits a port or pin argument may have. */
#define NUMBER_DIGITS_MAX   3u

/**
 * @brief Parse a decimal port or pin number.
 *
 * @param s  Argument text
 * @return The value (0..255), or -1 for an empty, non-decimal or larger value
 */
static int
parse_number(const char *s)
{
    unsigned value = 0;
    unsigned digits = 0;

    if (*s == '\0') {
        return -1;
    }
    while (*s != '\0') {
        if (*s < '0' || *s > '9' || ++digits > NUMBER_DIGITS_MAX) {
            return -1;
        }
        value = value * 10u + (unsigned)(*s - '0');
        s++;
    }
    return (value <= UINT8_MAX) ? (int)value : -1;
}

/**
 * @brief Parse a port argument: a decimal number, or J/j for port J.
 *
 * @param s  Argument text
 * @return The port number, or -1 if @p s is neither
 */
static int
parse_port(const char *s)
{
    if ((s[0] == 'J' || s[0] == 'j') && s[1] == '\0') {
        return GPIO_PORT_J;
    }
    return parse_number(s);
}

/**
 * @brief Print a pin's name: "P<port>.<pin>", or "PJ.<pin>" for port J.
 *
 * @param port  Port number
 * @param pin   Pin within the port
 */
static void
print_pin(int port, int pin)
{
    if (port == GPIO_PORT_J) {
        SHELL_PRINTF("PJ.%u", (unsigned)pin);
    } else {
        SHELL_PRINTF("P%u.%u", (unsigned)port, (unsigned)pin);
    }
}

/**
 * @brief Print "Error: <pin> not available" for a pin the driver refused.
 *
 * @param port  Port number
 * @param pin   Pin within the port
 */
static void
print_unavailable(int port, int pin)
{
    SHELL_PRINTF("Error: ");
    print_pin(port, pin);
    SHELL_PRINTF(" not available\n");
}

/**
 * @brief Read a pin and print its level with its owner or direction.
 *
 * @param port   Port number
 * @param pin    Pin within the port
 * @param owner  The pin's owner, or NULL for a free pin
 */
static void
gpio_show(int port, int pin, const char *owner)
{
    const char *state = owner;
    int level = tiku_gpio_read((uint8_t)port, (uint8_t)pin);

    if (level < 0) {
        print_unavailable(port, pin);
        return;
    }
    if (state == NULL) {
        state = (tiku_gpio_get_dir((uint8_t)port, (uint8_t)pin) == 1) ?
                "output" : "input";
    }
    print_pin(port, pin);
    SHELL_PRINTF(" = %u (%s)\n", (unsigned)level, state);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_gpio(uint8_t argc, const char *argv[])
{
    const char *owner;
    int port;
    int pin;
    int rc;

    if (argc < 3 || argc > 4) {
        SHELL_PRINTF("Usage: gpio <port> <pin> [0|1|t|in]\n");
        SHELL_PRINTF("  ports and pins as listed in /dev/gpio; J = 255\n");
        return;
    }

    port = parse_port(argv[1]);
    pin = parse_number(argv[2]);
    if (port < 0 || pin < 0 || pin >= tiku_gpio_pin_count((uint8_t)port)) {
        SHELL_PRINTF("Error: invalid port/pin\n");
        return;
    }
    owner = tiku_gpio_owner((uint8_t)port, (uint8_t)pin);

    /* No value argument -- read the pin */
    if (argc == 3) {
        gpio_show(port, pin, owner);
        return;
    }

    /* Value argument -- write, toggle, or set input, unless the pin is owned */
    if (owner != NULL) {
        SHELL_PRINTF("Error: ");
        print_pin(port, pin);
        SHELL_PRINTF(" owned by %s\n", owner);
        return;
    }
    if (strcmp(argv[3], "0") == 0 || strcmp(argv[3], "1") == 0) {
        rc = tiku_gpio_write((uint8_t)port, (uint8_t)pin,
                             (uint8_t)(argv[3][0] - '0'));
    } else if (strcmp(argv[3], "t") == 0) {
        rc = tiku_gpio_toggle((uint8_t)port, (uint8_t)pin);
    } else if (strcmp(argv[3], "in") == 0) {
        rc = tiku_gpio_dir_in((uint8_t)port, (uint8_t)pin);
    } else {
        SHELL_PRINTF("Error: value must be 0, 1, t, or in\n");
        return;
    }
    if (rc < 0) {
        print_unavailable(port, pin);
        return;
    }

    print_pin(port, pin);
    if (argv[3][0] == 'i') {
        SHELL_PRINTF(" -> input\n");
    } else if (argv[3][0] == 't') {
        SHELL_PRINTF(" -> %u\n",
                     (unsigned)tiku_gpio_read((uint8_t)port, (uint8_t)pin));
    } else {
        SHELL_PRINTF(" -> %c\n", argv[3][0]);
    }
}
