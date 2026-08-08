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

/** @brief Reject a port/pin pair the silicon does not have. */
#define GPIO_BAD(port, pin) \
    (((port) > RA8P1_PORT_MAX) || ((pin) > 15U))

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
    /* PDR alone: PMR stays 0 so the pin is a general I/O, and PODR stays 0 so
     * the pin starts low rather than at whatever it last drove. */
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
    /* A write claims the pin as an output, which is what the interface means
     * by it: PODR on a pin still configured as an input drives nothing, and
     * the pin then reads back whatever the board holds it at.  Direction and
     * level go into one PFS write so the pin never drives the wrong level
     * first.  A pin that already drives skips the interlock, leaving the
     * bit-bang path a single read. */
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
    /* Through the write path, so a toggle claims the pin the same way. */
    podr = TIKU_REG32(RA8P1_PORT_PCNTR1(port)) >> RA8P1_PORT_PODR_SHIFT;
    return tiku_gpio_arch_write(port, pin,
                                (podr & (1UL << pin)) ? 0U : 1U);
}

int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin)
{
    if (GPIO_BAD(port, pin)) { return -1; }
    /* PIDR reflects the pin, not the drive register, so this reads back an
     * output that a short is holding low as well as a real input. */
    return (int8_t)((TIKU_REG32(RA8P1_PORT_PCNTR2(port)) >> pin) & 1UL);
}

int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin)
{
    if (GPIO_BAD(port, pin)) { return -1; }
    return (int8_t)((TIKU_REG32(RA8P1_PORT_PCNTR1(port)) >> pin) & 1UL);
}
