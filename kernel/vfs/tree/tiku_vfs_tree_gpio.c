/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_gpio.c - /dev/gpio, /dev/gpio_dir and /dev/gpio_owner nodes.
 *
 * Pin-level GPIO through the filesystem.  Every pin file shares one read and
 * one write handler, which find their pin from tiku_vfs_serving(); the ports
 * and their widths come from tiku_gpio_geometry.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_gpio.h"
#include <interfaces/gpio/tiku_gpio.h>
#include <interfaces/gpio/tiku_gpio_owner.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* WORKERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief What a write to a pin file asks for. */
typedef enum {
    GPIO_ACT_NONE,          /**< Text not recognised */
    GPIO_ACT_LOW,           /**< Write a low level */
    GPIO_ACT_HIGH,          /**< Write a high level */
    GPIO_ACT_TOGGLE,        /**< Toggle the output level */
    GPIO_ACT_INPUT          /**< Select the platform's input configuration */
} gpio_action_t;

/**
 * @brief Direction character for one pin of a /dev/gpio_dir summary.
 *
 * @param port  Port number
 * @param pin   Pin within the port
 * @return 'O' output, 'I' input, '?' for an owned pin or an unreadable one
 */
static char
gpio_dir_char(uint8_t port, uint8_t pin)
{
    int dir;

    if (tiku_gpio_owner(port, pin) != NULL) {
        return '?';
    }
    dir = tiku_gpio_get_dir(port, pin);
    if (dir == 1) {
        return 'O';
    }
    return (dir == 0) ? 'I' : '?';
}

/**
 * @brief Shared renderer for one port's direction summary.
 *
 * One character per pin plus a newline, so an eight-pin port with pins 2 and
 * 3 as outputs reads "IIOOIIII\n".  Wrapped per port by PORT_WRAPPERS().
 *
 * @param port  Port number
 * @param buf   Output buffer for the rendered text
 * @param max   Capacity of @p buf in bytes
 * @return Length of the whole summary (snprintf-style)
 */
static int
gpio_dir_read(uint8_t port, char *buf, size_t max)
{
    char line[TIKU_GPIO_PORT_PINS_MAX + 2u];
    uint8_t count = tiku_gpio_pin_count(port);
    uint8_t pin;

    for (pin = 0; pin < count; pin++) {
        line[pin] = gpio_dir_char(port, pin);
    }
    line[count] = '\n';
    line[count + 1u] = '\0';
    return snprintf(buf, max, "%s", line);
}

/**
 * @brief Shared renderer for one port's /dev/gpio_owner file.
 *
 * One "<pin> <owner>\n" line per pin, where a free pin's owner reads "free".
 *
 * @param port  Port number
 * @param buf   Output buffer for the rendered text
 * @param max   Capacity of @p buf in bytes
 * @return Length of the whole listing (snprintf-style)
 */
static int
gpio_owners_read(uint8_t port, char *buf, size_t max)
{
    uint8_t count = tiku_gpio_pin_count(port);
    uint8_t pin;
    size_t total = 0;

    for (pin = 0; pin < count; pin++) {
        const char *owner = tiku_gpio_owner(port, pin);
        size_t at = (total < max) ? total : max;
        int n = snprintf(buf + at, max - at, "%u %s\n", (unsigned)pin,
                         (owner != NULL) ? owner : "free");

        if (n < 0) {
            return TIKU_VFS_ERR;
        }
        total += (size_t)n;
    }
    return (int)total;
}

/**
 * @brief Shared read worker for one GPIO pin.
 *
 * Renders the level the driver reads back as "0\n" or "1\n".
 *
 * @param port  Port number
 * @param pin   Pin within the port
 * @param buf   Output buffer for the rendered text
 * @param max   Capacity of @p buf in bytes
 * @return Bytes written, or TIKU_VFS_ENOTSUP when the driver rejects the pin
 */
static int
gpio_pin_read(uint8_t port, uint8_t pin, char *buf, size_t max)
{
    int v = tiku_gpio_read(port, pin);

    if (v < 0) {
        return TIKU_VFS_ENOTSUP;
    }
    return snprintf(buf, max, "%u\n", (unsigned)v);
}

/**
 * @brief Parse the text written to a pin file.
 *
 * Accepts "0", "1", "t", "i" and "in", each with optional trailing
 * whitespace.
 *
 * @param buf  Input text
 * @param len  Input length in bytes
 * @return The requested action, or GPIO_ACT_NONE for anything else
 */
static gpio_action_t
gpio_parse_action(const char *buf, size_t len)
{
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                       buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
        len--;
    }
    if (len == 2 && buf[0] == 'i' && buf[1] == 'n') {
        return GPIO_ACT_INPUT;
    }
    if (len != 1) {
        return GPIO_ACT_NONE;
    }
    switch (buf[0]) {
    case '0':
        return GPIO_ACT_LOW;
    case '1':
        return GPIO_ACT_HIGH;
    case 't':
        return GPIO_ACT_TOGGLE;
    case 'i':
        return GPIO_ACT_INPUT;
    default:
        return GPIO_ACT_NONE;
    }
}

/**
 * @brief Shared write worker for one GPIO pin.
 *
 * '0' and '1' go to tiku_gpio_write(), 't' to tiku_gpio_toggle(), and 'i' or
 * "in" to tiku_gpio_dir_in().  The text is checked before the pin's owner, so
 * unrecognised text is EINVAL on an owned pin as well.
 *
 * @param port  Port number
 * @param pin   Pin within the port
 * @param buf   Input text
 * @param len   Input length in bytes
 * @return TIKU_VFS_OK, TIKU_VFS_EINVAL for unrecognised text, TIKU_VFS_EBUSY
 *         for an owned pin, or TIKU_VFS_ENOTSUP when the driver refuses
 */
static int
gpio_pin_write(uint8_t port, uint8_t pin, const char *buf, size_t len)
{
    gpio_action_t action = gpio_parse_action(buf, len);
    int rc;

    if (action == GPIO_ACT_NONE) {
        return TIKU_VFS_EINVAL;
    }
    if (tiku_gpio_owner(port, pin) != NULL) {
        return TIKU_VFS_EBUSY;
    }
    switch (action) {
    case GPIO_ACT_LOW:
        rc = tiku_gpio_write(port, pin, 0);
        break;
    case GPIO_ACT_HIGH:
        rc = tiku_gpio_write(port, pin, 1);
        break;
    case GPIO_ACT_TOGGLE:
        rc = tiku_gpio_toggle(port, pin);
        break;
    default:                /* GPIO_ACT_INPUT */
        rc = tiku_gpio_dir_in(port, pin);
        break;
    }
    return (rc == TIKU_GPIO_OK) ? TIKU_VFS_OK : TIKU_VFS_ENOTSUP;
}

/*---------------------------------------------------------------------------*/
/* PER-PORT WRAPPERS                                                         */
/*---------------------------------------------------------------------------*/

/** @brief Generate one port's direction summary and owner listing readers. */
#define PORT_WRAPPERS(p, n)                                                 \
    static int dir_##p(char *buf, size_t max) {                             \
        return gpio_dir_read(p, buf, max);                                  \
    }                                                                       \
    static int owners_##p(char *buf, size_t max) {                          \
        return gpio_owners_read(p, buf, max);                               \
    }

TIKU_GPIO_PORTS(PORT_WRAPPERS)

static int gpio_pin_file_read(char *buf, size_t max);
static int gpio_pin_file_write(const char *buf, size_t len);

/*---------------------------------------------------------------------------*/
/* NODE TABLES                                                               */
/*---------------------------------------------------------------------------*/

/** @brief Expand X(port, pin) for pins 0..5 of port @p p. */
#define PINS_6(X, p)  X(p, 0) X(p, 1) X(p, 2) X(p, 3) X(p, 4) X(p, 5)

/** @brief Expand X(port, pin) for pins 0..7 of port @p p. */
#define PINS_8(X, p)  PINS_6(X, p) X(p, 6) X(p, 7)

/** @brief Expand X(port, pin) for pins 0..15 of port @p p. */
#define PINS_16(X, p)                                                       \
    PINS_8(X, p) X(p, 8) X(p, 9) X(p, 10) X(p, 11) X(p, 12) X(p, 13)        \
    X(p, 14) X(p, 15)

/** @brief Expand X(port, pin) for pins 0..31 of port @p p. */
#define PINS_32(X, p)                                                       \
    PINS_16(X, p) X(p, 16) X(p, 17) X(p, 18) X(p, 19) X(p, 20) X(p, 21)     \
    X(p, 22) X(p, 23) X(p, 24) X(p, 25) X(p, 26) X(p, 27) X(p, 28)          \
    X(p, 29) X(p, 30) X(p, 31)

/* A pin file holds a level; the summaries and owner listings hold text. */
static const tiku_vfs_desc_t desc_gpio = TIKU_VFS_DESC(
    TIKU_VFS_T_BOOL, TIKU_VFS_U_BOOL, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t desc_text = TIKU_VFS_DESC(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);

/** @brief One pin file; actuating a pin needs TIKU_VFS_CAP_HW. */
#define PIN_NODE(p, b)                                                      \
    { #b, TIKU_VFS_FILE, gpio_pin_file_read, gpio_pin_file_write, NULL, 0,  \
      &desc_gpio, NULL, TIKU_VFS_CAP_HW },

/** @brief One port's pin table. */
#define PORT_TABLE(p, n)                                                    \
    static const tiku_vfs_node_t pins_##p[] = { PINS_##n(PIN_NODE, p) };

TIKU_GPIO_PORTS(PORT_TABLE)

/** @brief One /dev/gpio/<port> directory. */
#define PORT_DIR(p, n)                                                      \
    { #p, TIKU_VFS_DIR, NULL, NULL, pins_##p, n },

/** @brief One /dev/gpio_dir/<port> summary file. */
#define PORT_SUMMARY(p, n)                                                  \
    { #p, TIKU_VFS_FILE, dir_##p, NULL, NULL, 0, &desc_text },

/** @brief One /dev/gpio_owner/<port> listing file. */
#define PORT_OWNERS(p, n)                                                   \
    { #p, TIKU_VFS_FILE, owners_##p, NULL, NULL, 0, &desc_text },

/*
 * The three exported tables hold one entry per port in the platform geometry,
 * in its order; tiku_vfs_tree_dev.c attaches them as /dev/gpio, /dev/gpio_dir
 * and /dev/gpio_owner with TIKU_VFS_TREE_GPIO_NPORTS entries each.
 */
const tiku_vfs_node_t tiku_vfs_tree_gpio_children[] = {
    TIKU_GPIO_PORTS(PORT_DIR)
};

const tiku_vfs_node_t tiku_vfs_tree_gpio_dir_children[] = {
    TIKU_GPIO_PORTS(PORT_SUMMARY)
};

const tiku_vfs_node_t tiku_vfs_tree_gpio_owner_children[] = {
    TIKU_GPIO_PORTS(PORT_OWNERS)
};

_Static_assert(sizeof(tiku_vfs_tree_gpio_children) /
               sizeof(tiku_vfs_tree_gpio_children[0])
               == TIKU_VFS_TREE_GPIO_NPORTS,
               "TIKU_VFS_TREE_GPIO_NPORTS out of sync");

/** @brief Fail the build when a port is wider than gpio_dir_read()'s line. */
#define PORT_WIDTH_CHECK(p, n)                                              \
    _Static_assert((n) <= TIKU_GPIO_PORT_PINS_MAX,                          \
                   "port " #p " is wider than TIKU_GPIO_PORT_PINS_MAX");

TIKU_GPIO_PORTS(PORT_WIDTH_CHECK)

/*---------------------------------------------------------------------------*/
/* PIN FILE HANDLERS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Find the port and pin of the pin file the VFS is serving.
 *
 * @param port  Out: port number
 * @param pin   Out: pin within the port
 * @return 1 when tiku_vfs_serving() is a pin file, 0 otherwise
 */
static int
gpio_served_pin(uint8_t *port, uint8_t *pin)
{
    uintptr_t at = (uintptr_t)tiku_vfs_serving();

#define PIN_OF(p, n)                                                        \
    if (at - (uintptr_t)pins_##p < sizeof pins_##p) {                       \
        *port = p;                                                          \
        *pin = (uint8_t)((at - (uintptr_t)pins_##p) / sizeof pins_##p[0]);  \
        return 1;                                                           \
    }
    TIKU_GPIO_PORTS(PIN_OF)
#undef PIN_OF
    return 0;
}

/** @brief Read handler shared by every /dev/gpio pin file. */
static int
gpio_pin_file_read(char *buf, size_t max)
{
    uint8_t port, pin;

    if (!gpio_served_pin(&port, &pin)) {
        return TIKU_VFS_ERR;
    }
    return gpio_pin_read(port, pin, buf, max);
}

/** @brief Write handler shared by every /dev/gpio pin file. */
static int
gpio_pin_file_write(const char *buf, size_t len)
{
    uint8_t port, pin;

    if (!gpio_served_pin(&port, &pin)) {
        return TIKU_VFS_ERR;
    }
    return gpio_pin_write(port, pin, buf, len);
}

/*---------------------------------------------------------------------------*/
/* DRIVER NOTIFY — post TIKU_EVENT_VFS to /dev/gpio watchers on an edge      */
/*---------------------------------------------------------------------------*/

void
tiku_vfs_tree_gpio_notify(uint8_t port, uint8_t pin)
{
    if (pin >= tiku_gpio_pin_count(port)) {
        return;
    }
    switch (port) {
#define NOTIFY_PORT(p, n)                                                   \
    case p:                                                                 \
        tiku_vfs_notify(&pins_##p[pin]);                                    \
        break;
    TIKU_GPIO_PORTS(NOTIFY_PORT)
#undef NOTIFY_PORT
    default:
        break;
    }
}
