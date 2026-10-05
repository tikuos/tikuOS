/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_owner.h - cooperative GPIO pin ownership.
 *
 * A bounded claim table records which driver holds a pin, beside the pins the
 * board reserves and the pins an architecture check finds routed elsewhere.
 * The gpio shell command and the /dev/gpio nodes refuse to change owned pins.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_GPIO_OWNER_H_
#define TIKU_GPIO_OWNER_H_

#include <stdint.h>

/** @brief Number of claims the table holds at once. */
#ifndef TIKU_GPIO_CLAIM_LIMIT
#define TIKU_GPIO_CLAIM_LIMIT       16
#endif

/** @brief Longest owner name tiku_gpio_claim() accepts, in bytes. */
#define TIKU_GPIO_OWNER_NAME_MAX    23u

/**
 * @brief Claim a pin for a driver.
 *
 * Refused for a pin outside the platform geometry, reserved by the board,
 * routed to a peripheral, unreadable or held by another owner, or when the
 * table is full.  A repeated claim by the same owner succeeds.
 *
 * @param port   Port number as the raw GPIO interface takes it
 * @param pin    Pin within the port
 * @param owner  1..TIKU_GPIO_OWNER_NAME_MAX printable bytes without spaces,
 *               and none of the labels tiku_gpio_owner() reports
 * @return 0 on success, -1 if refused
 * @note Scheduler context only.  The table keeps the @p owner pointer, so the
 *       name needs static lifetime.  The pin's configuration is not touched,
 *       and raw HAL calls do not consult the table.
 */
int tiku_gpio_claim(uint8_t port, uint8_t pin, const char *owner);

/**
 * @brief Release a pin claimed by @p owner.
 *
 * @param port   Port number as the raw GPIO interface takes it
 * @param pin    Pin within the port
 * @param owner  The name the pin was claimed with
 * @return 0 on success, -1 if @p owner holds no claim on the pin
 * @note Scheduler context only.
 */
int tiku_gpio_release(uint8_t port, uint8_t pin, const char *owner);

/**
 * @brief Report who owns a pin.
 *
 * @param port  Port number as the raw GPIO interface takes it
 * @param pin   Pin within the port
 * @return NULL for a free pin; otherwise "console" or "wifi" for a board
 *         reservation, the claiming driver's name, "peripheral" for a pin
 *         routed to a peripheral, "unreadable" when the architecture check
 *         cannot read the pin, or "invalid" outside the platform geometry
 * @note Scheduler context only: the claim table is read without locking.
 */
const char *tiku_gpio_owner(uint8_t port, uint8_t pin);

/**
 * @brief Architecture hook: is a pin routed away from the GPIO block?
 *
 * Each port's tiku_gpio_arch.c implements it by reading the pin's mux
 * configuration, which it leaves unchanged.
 *
 * @param port  Port number as the raw GPIO interface takes it
 * @param pin   Pin within the port
 * @return Positive when a peripheral holds the pin, 0 for a GPIO or unassigned
 *         pin, negative when the configuration cannot be read
 * @note Does not enable a port clock: a port whose clock is off reads as
 *       unassigned.
 */
int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin);

#endif /* TIKU_GPIO_OWNER_H_ */
