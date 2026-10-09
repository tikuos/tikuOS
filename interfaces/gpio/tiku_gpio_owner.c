/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_owner.c - cooperative GPIO pin ownership.
 *
 * A pin's owner is its board reservation, else its claim, else what the
 * architecture's mux check reports.  Nothing here reconfigures a pin.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_gpio_owner.h"
#include "tiku_gpio_geometry.h"
#include <stddef.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* CLAIM TABLE                                                               */
/*---------------------------------------------------------------------------*/

/** @brief One claimed pin. */
typedef struct {
    const char *owner;      /**< Claiming driver's name; NULL = free slot */
    uint8_t     port;       /**< Port number */
    uint8_t     pin;        /**< Pin within the port */
} gpio_claim_t;

static gpio_claim_t claims[TIKU_GPIO_CLAIM_LIMIT];

/** @brief Labels tiku_gpio_owner() or /dev/gpio_owner report; unclaimable. */
static const char *const reserved_names[] = {
    "free", "console", "peripheral", "unreadable", "invalid", "wifi"
};

/**
 * @brief Index of the claim on a pin.
 *
 * @param port  Port number
 * @param pin   Pin within the port
 * @return Slot index, or -1 when the pin has no claim
 */
static int
claim_index(uint8_t port, uint8_t pin)
{
    unsigned i;

    for (i = 0; i < TIKU_GPIO_CLAIM_LIMIT; i++) {
        if (claims[i].owner != NULL && claims[i].port == port &&
            claims[i].pin == pin) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Check an owner name against the tiku_gpio_claim() rules.
 *
 * @param owner  Candidate name
 * @return 1 when acceptable, 0 otherwise
 */
static int
name_valid(const char *owner)
{
    size_t n;
    size_t i;

    if (owner == NULL || owner[0] == '\0') {
        return 0;
    }
    for (n = 0; owner[n] != '\0'; n++) {
        if (n == TIKU_GPIO_OWNER_NAME_MAX ||
            (unsigned char)owner[n] <= ' ' || (unsigned char)owner[n] > '~') {
            return 0;
        }
    }
    for (i = 0; i < sizeof(reserved_names) / sizeof(reserved_names[0]); i++) {
        if (strcmp(owner, reserved_names[i]) == 0) {
            return 0;
        }
    }
    return 1;
}

/*---------------------------------------------------------------------------*/
/* BOARD RESERVATIONS                                                        */
/*---------------------------------------------------------------------------*/

/** @brief Pins per port where a platform splits a flat pin range into ports. */
#define BANK_PINS   8u

/** @brief True when (port, pin) names flat pin @p n on such a platform. */
#define IS_BANKED_PIN(port, pin, n) \
    ((port) == (n) / BANK_PINS + 1u && (pin) == (n) % BANK_PINS)

/**
 * @brief Board function holding a pin, or NULL.
 *
 * Covers the console UART's pins on every platform but the MSP430, whose mux
 * check reports them, and the Pico 2 W radio wiring when its driver is built.
 *
 * @param port  Port number
 * @param pin   Pin within the port
 * @return "console", "wifi" or NULL
 */
static const char *
board_owner(uint8_t port, uint8_t pin)
{
    /* Unused where a configuration reserves nothing. */
    (void)port;
    (void)pin;
#if defined(PLATFORM_NORDIC)
    /* Virtual port n is physical port n - 1. */
    if ((port == TIKU_BOARD_UART_TX_PORT + 1u &&
         pin == TIKU_BOARD_UART_TX_PIN) ||
        (port == TIKU_BOARD_UART_RX_PORT + 1u &&
         pin == TIKU_BOARD_UART_RX_PIN)) {
        return "console";
    }
#elif defined(PLATFORM_AMBIQ)
    if (IS_BANKED_PIN(port, pin, TIKU_BOARD_UART_TX_PIN) ||
        IS_BANKED_PIN(port, pin, TIKU_BOARD_UART_RX_PIN)) {
        return "console";
    }
#elif defined(PLATFORM_RP2350)
#if !defined(TIKU_CONSOLE_USB) || defined(TIKU_CONSOLE_BOTH)
    if (IS_BANKED_PIN(port, pin, TIKU_BOARD_UART_TX_PIN) ||
        IS_BANKED_PIN(port, pin, TIKU_BOARD_UART_RX_PIN)) {
        return "console";
    }
#endif
#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
    /* The CYW43439's gSPI wiring, including its GPIO-driven power enable. */
    if (IS_BANKED_PIN(port, pin, TIKU_BOARD_CYW43_WL_REG_ON_PIN) ||
        IS_BANKED_PIN(port, pin, TIKU_BOARD_CYW43_WL_DATA_PIN) ||
        IS_BANKED_PIN(port, pin, TIKU_BOARD_CYW43_WL_CS_PIN) ||
        IS_BANKED_PIN(port, pin, TIKU_BOARD_CYW43_WL_CLOCK_PIN)) {
        return "wifi";
    }
#endif
#elif defined(PLATFORM_ESP32C61)
    if (IS_BANKED_PIN(port, pin, TIKU_BOARD_UART0_TX_GPIO) ||
        IS_BANKED_PIN(port, pin, TIKU_BOARD_UART0_RX_GPIO)) {
        return "console";
    }
#elif defined(PLATFORM_ESP32C5)
#if !defined(TIKU_CONSOLE_JTAG)
    if (IS_BANKED_PIN(port, pin, TIKU_BOARD_UART_TX_PIN) ||
        IS_BANKED_PIN(port, pin, TIKU_BOARD_UART_RX_PIN)) { return "console"; }
#endif
    if (IS_BANKED_PIN(port, pin, TIKU_BOARD_USB_DM_PIN) ||
        IS_BANKED_PIN(port, pin, TIKU_BOARD_USB_DP_PIN)) { return "console"; }
    if (port >= 1 && port <= 4 && pin < 8 &&
        (TIKU_BOARD_GPIO_RESERVED & (1UL << ((port - 1u) * 8u + pin)))) { return "peripheral"; }
#elif defined(PLATFORM_STM32N6)
    if (port == TIKU_BOARD_UART_PORT &&
        (pin == TIKU_BOARD_UART_TX_PIN || pin == TIKU_BOARD_UART_RX_PIN)) {
        return "console";
    }
#elif defined(PLATFORM_RA8P1)
    if ((port == TIKU_BOARD_CONSOLE_TX_PORT &&
         pin == TIKU_BOARD_CONSOLE_TX_PIN) ||
        (port == TIKU_BOARD_CONSOLE_RX_PORT &&
         pin == TIKU_BOARD_CONSOLE_RX_PIN)) {
        return "console";
    }
#elif defined(PLATFORM_MSP430)
    /* The console's eUSCI pins have PxSEL set and read as "peripheral". */
#else
#error "tiku_gpio_owner.c: no board reservations for this platform"
#endif
    return NULL;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

const char *
tiku_gpio_owner(uint8_t port, uint8_t pin)
{
    const char *name;
    int slot;
    int routed;

    if (pin >= tiku_gpio_pin_count(port)) {
        return "invalid";
    }
    name = board_owner(port, pin);
    if (name != NULL) {
        return name;
    }
    slot = claim_index(port, pin);
    if (slot >= 0) {
        return claims[slot].owner;
    }
    routed = tiku_gpio_arch_is_peripheral(port, pin);
    if (routed > 0) {
        return "peripheral";
    }
    if (routed < 0) {
        return "unreadable";
    }
    return NULL;
}

int
tiku_gpio_claim(uint8_t port, uint8_t pin, const char *owner)
{
    int slot;
    unsigned i;

    if (!name_valid(owner) || pin >= tiku_gpio_pin_count(port) ||
        board_owner(port, pin) != NULL) {
        return -1;
    }
    slot = claim_index(port, pin);
    if (slot >= 0) {
        return (strcmp(claims[slot].owner, owner) == 0) ? 0 : -1;
    }
    if (tiku_gpio_arch_is_peripheral(port, pin) != 0) {
        return -1;
    }
    for (i = 0; i < TIKU_GPIO_CLAIM_LIMIT; i++) {
        if (claims[i].owner == NULL) {
            claims[i].port = port;
            claims[i].pin = pin;
            claims[i].owner = owner;
            return 0;
        }
    }
    return -1;
}

int
tiku_gpio_release(uint8_t port, uint8_t pin, const char *owner)
{
    int slot = claim_index(port, pin);

    if (owner == NULL || slot < 0 ||
        strcmp(claims[slot].owner, owner) != 0) {
        return -1;
    }
    claims[slot].owner = NULL;
    return 0;
}
