/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_geometry.h - GPIO port numbers and widths for each platform.
 *
 * One X-macro table per platform lists the ports the raw GPIO interface takes
 * and the register bits in each.  These are addressable bits, not a list of
 * bonded or header-accessible pins.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_GPIO_GEOMETRY_H_
#define TIKU_GPIO_GEOMETRY_H_

#include "tiku.h"
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* PORT TABLES                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_GPIO_PORTS(X)
 * @brief Expands X(port, pin_count) once per port, in the port numbering the
 *        raw GPIO interface takes.
 *
 * A pin count must have a PINS_<n> expander in tiku_vfs_tree_gpio.c (6, 8,
 * 16 and 32 do).
 */
#if defined(PLATFORM_NORDIC)
/* Virtual ports 1..3 are P0..P2; port 4 is P3 on parts that have it. */
#if TIKU_DEVICE_HAS_PORT4
#define TIKU_GPIO_PORTS(X) X(1, 32) X(2, 32) X(3, 32) X(4, 32)
#else
#define TIKU_GPIO_PORTS(X) X(1, 32) X(2, 32) X(3, 32)
#endif

#elif defined(PLATFORM_RP2350)
/* GP0..GP29 in banks of eight: port 1 pin 0 is GP0, port 4 pin 5 is GP29. */
#define TIKU_GPIO_PORTS(X) X(1, 8) X(2, 8) X(3, 8) X(4, 6)

#elif defined(PLATFORM_ESP32C61)
/* GPIO0..GPIO29 in banks of eight, numbered as on the RP2350. */
#define TIKU_GPIO_PORTS(X) X(1, 8) X(2, 8) X(3, 8) X(4, 6)

#elif defined(PLATFORM_AMBIQ)
/* Pads in banks of eight, pad = (port - 1) * 8 + pin: pads 0..127 on the
 * Apollo4 register map, pads 0..223 on the Apollo510. */
#define TIKU_GPIO_PORTS_APOLLO4(X)                                          \
    X(1, 8) X(2, 8) X(3, 8) X(4, 8) X(5, 8) X(6, 8) X(7, 8) X(8, 8)          \
    X(9, 8) X(10, 8) X(11, 8) X(12, 8) X(13, 8) X(14, 8) X(15, 8) X(16, 8)
#if TIKU_DEVICE_CS_TYPE_APOLLO4L
#define TIKU_GPIO_PORTS(X) TIKU_GPIO_PORTS_APOLLO4(X)
#else
#define TIKU_GPIO_PORTS(X)                                                  \
    TIKU_GPIO_PORTS_APOLLO4(X)                                              \
    X(17, 8) X(18, 8) X(19, 8) X(20, 8) X(21, 8) X(22, 8)                   \
    X(23, 8) X(24, 8) X(25, 8) X(26, 8) X(27, 8) X(28, 8)
#endif

#elif defined(PLATFORM_STM32N6)
/* GPIOA..GPIOH are 0..7 and GPION..GPIOQ are 13..16; the STM32N657 has no
 * GPIOI..GPIOM register blocks. */
#define TIKU_GPIO_PORTS(X)                                                  \
    X(0, 16) X(1, 16) X(2, 16) X(3, 16) X(4, 16) X(5, 16) X(6, 16)          \
    X(7, 16) X(13, 16) X(14, 16) X(15, 16) X(16, 16)

#elif defined(PLATFORM_RA8P1)
/* PORT0..PORT9 are 0..9 and PORTA..PORTD are 10..13. */
#define TIKU_GPIO_PORTS(X)                                                  \
    X(0, 16) X(1, 16) X(2, 16) X(3, 16) X(4, 16) X(5, 16) X(6, 16)          \
    X(7, 16) X(8, 16) X(9, 16) X(10, 16) X(11, 16) X(12, 16) X(13, 16)

#elif defined(PLATFORM_MSP430)
/* The device header's P1..P9, eight bits each; port J is numbered 255. */
#if TIKU_DEVICE_HAS_PORT1
#define TIKU_GPIO_PORT_1(X) X(1, 8)
#else
#define TIKU_GPIO_PORT_1(X)
#endif
#if TIKU_DEVICE_HAS_PORT2
#define TIKU_GPIO_PORT_2(X) X(2, 8)
#else
#define TIKU_GPIO_PORT_2(X)
#endif
#if TIKU_DEVICE_HAS_PORT3
#define TIKU_GPIO_PORT_3(X) X(3, 8)
#else
#define TIKU_GPIO_PORT_3(X)
#endif
#if TIKU_DEVICE_HAS_PORT4
#define TIKU_GPIO_PORT_4(X) X(4, 8)
#else
#define TIKU_GPIO_PORT_4(X)
#endif
#if TIKU_DEVICE_HAS_PORT5
#define TIKU_GPIO_PORT_5(X) X(5, 8)
#else
#define TIKU_GPIO_PORT_5(X)
#endif
#if TIKU_DEVICE_HAS_PORT6
#define TIKU_GPIO_PORT_6(X) X(6, 8)
#else
#define TIKU_GPIO_PORT_6(X)
#endif
#if TIKU_DEVICE_HAS_PORT7
#define TIKU_GPIO_PORT_7(X) X(7, 8)
#else
#define TIKU_GPIO_PORT_7(X)
#endif
#if TIKU_DEVICE_HAS_PORT8
#define TIKU_GPIO_PORT_8(X) X(8, 8)
#else
#define TIKU_GPIO_PORT_8(X)
#endif
#if TIKU_DEVICE_HAS_PORT9
#define TIKU_GPIO_PORT_9(X) X(9, 8)
#else
#define TIKU_GPIO_PORT_9(X)
#endif
#if TIKU_DEVICE_HAS_PORTJ
#define TIKU_GPIO_PORT_J(X) X(255, 8)
#else
#define TIKU_GPIO_PORT_J(X)
#endif
#define TIKU_GPIO_PORTS(X)                                                  \
    TIKU_GPIO_PORT_1(X) TIKU_GPIO_PORT_2(X) TIKU_GPIO_PORT_3(X)             \
    TIKU_GPIO_PORT_4(X) TIKU_GPIO_PORT_5(X) TIKU_GPIO_PORT_6(X)             \
    TIKU_GPIO_PORT_7(X) TIKU_GPIO_PORT_8(X) TIKU_GPIO_PORT_9(X)             \
    TIKU_GPIO_PORT_J(X)

#else
#error "tiku_gpio_geometry.h: no GPIO port table for this platform"
#endif

/*---------------------------------------------------------------------------*/
/* DERIVED VALUES                                                            */
/*---------------------------------------------------------------------------*/

/** @brief Pin count of the widest port in any table above. */
#define TIKU_GPIO_PORT_PINS_MAX     32u

/** @brief Adds one per port; summed by TIKU_GPIO_PORT_COUNT. */
#define TIKU_GPIO_COUNT_PORT(p, n)  + 1

/** @brief Number of ports in this platform's table. */
#define TIKU_GPIO_PORT_COUNT        (0 TIKU_GPIO_PORTS(TIKU_GPIO_COUNT_PORT))

/**
 * @brief Number of register bits in a GPIO port.
 *
 * @param port  Port number as the raw GPIO interface takes it
 * @return Pin count, or 0 when this platform has no such port
 * @note A lookup in a compile-time table: safe from any context, interrupt
 *       handlers included.
 */
static inline uint8_t
tiku_gpio_pin_count(uint8_t port)
{
    switch (port) {
#define TIKU_GPIO_WIDTH(p, n)                                               \
    case p:                                                                 \
        return n;
    TIKU_GPIO_PORTS(TIKU_GPIO_WIDTH)
#undef TIKU_GPIO_WIDTH
    default:
        return 0;
    }
}

#endif /* TIKU_GPIO_GEOMETRY_H_ */
