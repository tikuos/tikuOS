/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - RA8P1 GPIO.
 *
 * Two register views reach the same pin.  Configuration goes through PmnPFS,
 * which is per-pin and write-protected; the level goes through the port's
 * PCNTR registers, which are per-port and need no unlock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpio_arch.h"
#include "tiku_ra8p1_regs.h"

/** @brief True for a port or pin this silicon does not have. */
#define GPIO_BAD(port, pin) \
    (((port) > RA8P1_PORT_MAX) || ((pin) > 15U))

/** @brief PmnPFS.ISEL: the pin feeds an IRQn input. */
#define PFS_ISEL    (1UL << 14)

/** @brief PmnPFS.ASEL: the pin is an analog input. */
#define PFS_ASEL    (1UL << 15)

/**
 * @brief Write one PmnPFS register through the protect interlock.
 *
 * PFSWE cannot be set in the same access that clears B0WI; the sequence
 * below is the manual's (UM 21.2.8).
 *
 * @param port  Port index
 * @param pin   Pin number
 * @param val   Value to place in PmnPFS
 */
static void pfs_write(uint8_t port, uint8_t pin, uint32_t val)
{
    TIKU_REG8(RA8P1_PWPR_S) = 0x00U;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_PFSWE;
    TIKU_REG32(RA8P1_PFS(port, pin)) = val;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_B0WI;
}

void tiku_ra8p1_gpio_init_output(uint8_t port, uint8_t pin)
{
    if (GPIO_BAD(port, pin)) { return; }
    /* PDR alone: PMR = 0 makes the pin general I/O, and PODR = 0 starts it
     * low. */
    pfs_write(port, pin, RA8P1_PFS_PDR);
}

void tiku_ra8p1_gpio_init_input(uint8_t port, uint8_t pin)
{
    if (GPIO_BAD(port, pin)) { return; }
    pfs_write(port, pin, 0UL);
}

void tiku_ra8p1_gpio_init_peripheral(uint8_t port, uint8_t pin, uint8_t psel)
{
    if (GPIO_BAD(port, pin)) { return; }
    pfs_write(port, pin,
              ((uint32_t)(psel & 0x1FU) << RA8P1_PFS_PSEL_SHIFT) |
              RA8P1_PFS_PMR);
}

void tiku_ra8p1_gpio_set(uint8_t port, uint8_t pin, uint8_t value)
{
    if (GPIO_BAD(port, pin)) { return; }
    if (value != 0U) {
        TIKU_REG32(RA8P1_PORT_PCNTR3(port)) = (1UL << pin);
    } else {
        TIKU_REG32(RA8P1_PORT_PCNTR3(port)) =
            (1UL << (pin + RA8P1_PORT_PORR_SHIFT));
    }
}

void tiku_ra8p1_gpio_toggle(uint8_t port, uint8_t pin)
{
    uint32_t podr;

    if (GPIO_BAD(port, pin)) { return; }
    podr = TIKU_REG32(RA8P1_PORT_PCNTR1(port)) >> RA8P1_PORT_PODR_SHIFT;
    tiku_ra8p1_gpio_set(port, pin, (podr & (1UL << pin)) ? 0U : 1U);
}

/*---------------------------------------------------------------------------*/
/* Kernel-facing contract                                                    */
/*---------------------------------------------------------------------------*/

int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin)
{
    if (GPIO_BAD(port, pin)) { return -1; }
    tiku_ra8p1_gpio_init_output(port, pin);
    return 0;
}

int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin)
{
    if (GPIO_BAD(port, pin)) { return -1; }
    tiku_ra8p1_gpio_init_input(port, pin);
    return 0;
}

int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val)
{
    if (GPIO_BAD(port, pin)) { return -1; }
    /* A write to an input pin makes it an output: direction and level go in
     * one PFS write, so the pin never drives the old level first.  A pin
     * already driving is set through PCNTR3, without the PFS interlock. */
    if ((TIKU_REG32(RA8P1_PORT_PCNTR1(port)) & (1UL << pin)) == 0UL) {
        pfs_write(port, pin,
                  RA8P1_PFS_PDR | ((val != 0U) ? RA8P1_PFS_PODR : 0UL));
        return 0;
    }
    tiku_ra8p1_gpio_set(port, pin, val);
    return 0;
}

int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin)
{
    uint32_t podr;

    if (GPIO_BAD(port, pin)) { return -1; }
    /* Through tiku_gpio_arch_write(), so a toggle also makes an input pin an
     * output. */
    podr = TIKU_REG32(RA8P1_PORT_PCNTR1(port)) >> RA8P1_PORT_PODR_SHIFT;
    return tiku_gpio_arch_write(port, pin,
                                (podr & (1UL << pin)) ? 0U : 1U);
}

int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin)
{
    if (GPIO_BAD(port, pin)) { return -1; }
    /* PIDR is the pin's level, so an output reads what the pin is at, which
     * a short can hold low. */
    return (int8_t)((TIKU_REG32(RA8P1_PORT_PCNTR2(port)) >> pin) & 1UL);
}

int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin)
{
    if (GPIO_BAD(port, pin)) { return -1; }
    return (int8_t)((TIKU_REG32(RA8P1_PORT_PCNTR1(port)) >> pin) & 1UL);
}

/**
 * @brief Report whether PmnPFS gives the pin away: PMR to the peripheral PSEL
 *        names, ASEL to an analog input or ISEL to an IRQn input.
 *
 * @return 1 if so, 0 if not, -1 for a pin out of range
 */
int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin)
{
    if (GPIO_BAD(port, pin)) { return -1; }
    return ((TIKU_REG32(RA8P1_PFS(port, pin)) &
             (RA8P1_PFS_PMR | PFS_ASEL | PFS_ISEL)) != 0UL) ? 1 : 0;
}
