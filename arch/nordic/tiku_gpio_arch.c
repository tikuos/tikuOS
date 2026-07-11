/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - nRF54L GPIO primitives (P0/P1/P2)
 *
 * The nRF GPIO block drives outputs via OUTSET/OUTCLR, reads inputs from IN,
 * and configures each pin through PIN_CNF[pin] (DIR bit0, INPUT-buffer bit1,
 * PULL bits2-3).  Helpers take a physical port (0/1/2) matching the board
 * silk P<port>.<pin>; see tiku_gpio_arch.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_gpio_arch.h>
#include <arch/nordic/mdk/nrf54l15.h>

/*---------------------------------------------------------------------------*/
/* Port -> base pointer                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Map a physical port number to its GPIO register block.
 *
 * @param port 0 = P0, 1 = P1, 2 = P2.
 * @return Pointer to the port's NRF_GPIO_Type, or NULL if out of range.
 */
static NRF_GPIO_Type *tiku_nordic_gpio_base(uint8_t port)
{
    switch (port) {
    case 0u:  return NRF_P0_S;
    case 1u:  return NRF_P1_S;
    case 2u:  return NRF_P2_S;
    default:  return (NRF_GPIO_Type *)0;
    }
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

void tiku_nordic_gpio_init_output(uint8_t port, uint8_t pin, uint8_t init_level)
{
    NRF_GPIO_Type *g = tiku_nordic_gpio_base(port);
    uint32_t bit = (1UL << (pin & 0x1Fu));

    if (g == (NRF_GPIO_Type *)0) {
        return;
    }
    /* Drive the requested level BEFORE enabling the output so the pin never
     * glitches to the wrong state. */
    if (init_level != 0u) {
        g->OUTSET = bit;
    } else {
        g->OUTCLR = bit;
    }
    /* DIR=Output, input buffer disconnected, no pull. */
    g->PIN_CNF[pin & 0x1Fu] =
        (GPIO_PIN_CNF_DIR_Output      << GPIO_PIN_CNF_DIR_Pos)   |
        (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
        (GPIO_PIN_CNF_PULL_Disabled   << GPIO_PIN_CNF_PULL_Pos);
    g->DIRSET = bit;
}

void tiku_nordic_gpio_init_input_pullup(uint8_t port, uint8_t pin)
{
    NRF_GPIO_Type *g = tiku_nordic_gpio_base(port);

    if (g == (NRF_GPIO_Type *)0) {
        return;
    }
    g->DIRCLR = (1UL << (pin & 0x1Fu));
    /* DIR=Input, input buffer connected, pull-up enabled. */
    g->PIN_CNF[pin & 0x1Fu] =
        (GPIO_PIN_CNF_DIR_Input     << GPIO_PIN_CNF_DIR_Pos)   |
        (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
        (GPIO_PIN_CNF_PULL_Pullup   << GPIO_PIN_CNF_PULL_Pos);
}

void tiku_nordic_gpio_set(uint8_t port, uint8_t pin, uint8_t level)
{
    NRF_GPIO_Type *g = tiku_nordic_gpio_base(port);
    uint32_t bit = (1UL << (pin & 0x1Fu));

    if (g == (NRF_GPIO_Type *)0) {
        return;
    }
    if (level != 0u) {
        g->OUTSET = bit;
    } else {
        g->OUTCLR = bit;
    }
}

void tiku_nordic_gpio_toggle(uint8_t port, uint8_t pin)
{
    NRF_GPIO_Type *g = tiku_nordic_gpio_base(port);

    if (g == (NRF_GPIO_Type *)0) {
        return;
    }
    g->OUT ^= (1UL << (pin & 0x1Fu));
}

uint8_t tiku_nordic_gpio_read(uint8_t port, uint8_t pin)
{
    NRF_GPIO_Type *g = tiku_nordic_gpio_base(port);
    uint32_t sh;

    if (g == (NRF_GPIO_Type *)0) {
        return 0u;
    }
    sh = (uint32_t)(pin & 0x1Fu);
    /* Return the pin's LOGICAL level.  For an output pin the input buffer is
     * disconnected (see init_output), so IN reads 0 no matter what is driven --
     * read the driven level from OUT instead.  Input pins read the sensed level
     * from IN.  This matches the read-back-what-you-drive semantics of the
     * msp430/rp2350/ambiq ports. */
    if ((g->DIR >> sh) & 0x1u) {
        return (uint8_t)((g->OUT >> sh) & 0x1u);
    }
    return (uint8_t)((g->IN >> sh) & 0x1u);
}

/*---------------------------------------------------------------------------*/
/* Generic GPIO HAL (port/pin, used by the VFS /dev/gpio tree + shell)       */
/*---------------------------------------------------------------------------*/

/*
 * The VFS/shell layer uses 1-based virtual ports (port 1 = P0, 2 = P1, 3 = P2)
 * matching TIKU_DEVICE_HAS_PORT1..3.  These wrappers translate to the physical
 * (0-based) port the helpers above take, validate the range, and return the
 * int8_t status the HAL expects (0 = ok, -1 = out of range).
 */

/** @brief Valid virtual port (1..3) and pin (0..31)? */
static int tiku_nordic_gpio_valid(uint8_t port, uint8_t pin)
{
    return (port >= 1u && port <= 3u && pin <= 31u);
}

int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin)
{
    if (!tiku_nordic_gpio_valid(port, pin)) {
        return -1;
    }
    tiku_nordic_gpio_init_output((uint8_t)(port - 1u), pin, 0u);
    return 0;
}

int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin)
{
    NRF_GPIO_Type *g;

    if (!tiku_nordic_gpio_valid(port, pin)) {
        return -1;
    }
    g = tiku_nordic_gpio_base((uint8_t)(port - 1u));
    if (g == (NRF_GPIO_Type *)0) {
        return -1;
    }
    g->DIRCLR = (1UL << pin);
    /* DIR=input, input buffer connected, no pull (PIN_CNF all-zero fields). */
    g->PIN_CNF[pin] = 0UL;
    return 0;
}

int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val)
{
    if (!tiku_nordic_gpio_valid(port, pin)) {
        return -1;
    }
    /* Claim as output and drive the requested level. */
    tiku_nordic_gpio_init_output((uint8_t)(port - 1u), pin, (uint8_t)(val ? 1u : 0u));
    return 0;
}

int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin)
{
    if (!tiku_nordic_gpio_valid(port, pin)) {
        return -1;
    }
    tiku_nordic_gpio_toggle((uint8_t)(port - 1u), pin);
    return 0;
}

int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin)
{
    if (!tiku_nordic_gpio_valid(port, pin)) {
        return -1;
    }
    return (int8_t)tiku_nordic_gpio_read((uint8_t)(port - 1u), pin);
}

int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin)
{
    NRF_GPIO_Type *g;

    if (!tiku_nordic_gpio_valid(port, pin)) {
        return -1;
    }
    g = tiku_nordic_gpio_base((uint8_t)(port - 1u));
    if (g == (NRF_GPIO_Type *)0) {
        return -1;
    }
    /* 1 = output, 0 = input. */
    return (int8_t)((g->DIR >> pin) & 0x1u);
}
