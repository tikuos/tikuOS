/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - RP2350 GPIO driver
 *
 * Maps the (port, pin) tuple onto a flat GP index (0..31) and drives
 * SIO + IO_BANK0 + PADS_BANK0 directly. We do not touch pins above
 * GP29 — the QSPI bank is excluded so a `gpio 4 7` shell command can't
 * accidentally short the flash chip.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpio_arch.h"
#include "tiku_rp2350_regs.h"
#include <stdint.h>

#define MAX_GP_PIN  29U   /* GP0..GP29 are exposed on the Pico 2 W header */

static inline int8_t gp_index(uint8_t port, uint8_t pin) {
    if (port < 1U || port > 4U || pin > 7U) {
        return -1;
    }
    uint8_t idx = (uint8_t)(((port - 1U) * 8U) + pin);
    if (idx > MAX_GP_PIN) {
        return -1;
    }
    return (int8_t)idx;
}

/*---------------------------------------------------------------------------*/
/* Per-pin direct helpers                                                    */
/*---------------------------------------------------------------------------*/

void tiku_rp2350_gpio_init_output(uint8_t pin) {
    if (pin > MAX_GP_PIN) {
        return;
    }
    /* Function = SIO. Keep input-enable on so SIO_GPIO_IN reflects
     * the level being driven (the GPIO API contract is "you can read
     * back what you wrote to an output pin", which MSP430 satisfies
     * for free; on RP2350 SIO_GPIO_IN reads 0 if the pad input
     * buffer is disabled, even when the pin is electrically high). */
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(pin)) =
        RP2350_PADS_IE | RP2350_PADS_DRIVE_4MA;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(pin)) =
        RP2350_IO_FUNC_SIO;
    /* Output low, then enable output drive. */
    _RP2350_REG_CLR(RP2350_SIO_GPIO_OUT, (1U << pin));
    _RP2350_REG_SET(RP2350_SIO_GPIO_OE,  (1U << pin));
}

void tiku_rp2350_gpio_set(uint8_t pin, uint8_t value) {
    if (pin > MAX_GP_PIN) {
        return;
    }
    if (value) {
        _RP2350_REG(RP2350_SIO_GPIO_OUT_SET) = (1U << pin);
    } else {
        _RP2350_REG(RP2350_SIO_GPIO_OUT_CLR) = (1U << pin);
    }
}

void tiku_rp2350_gpio_toggle(uint8_t pin) {
    if (pin > MAX_GP_PIN) {
        return;
    }
    _RP2350_REG(RP2350_SIO_GPIO_OUT_XOR) = (1U << pin);
}

/*---------------------------------------------------------------------------*/
/* HAL entry points                                                          */
/*---------------------------------------------------------------------------*/

int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin) {
    int8_t gp = gp_index(port, pin);
    if (gp < 0) {
        return -1;
    }
    tiku_rp2350_gpio_init_output((uint8_t)gp);
    return 0;
}

int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin) {
    int8_t gp = gp_index(port, pin);
    if (gp < 0) {
        return -1;
    }
    /* Function = SIO, input enable on pad, pull-up enabled, schmitt on. */
    _RP2350_REG(RP2350_PADS_BANK0_GPIO((uint8_t)gp)) =
        RP2350_PADS_IE | RP2350_PADS_DRIVE_4MA |
        RP2350_PADS_PUE | RP2350_PADS_SCHMITT;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL((uint8_t)gp)) =
        RP2350_IO_FUNC_SIO;
    /* Output disable. */
    _RP2350_REG_CLR(RP2350_SIO_GPIO_OE, (1U << gp));
    return 0;
}

int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val) {
    int8_t gp = gp_index(port, pin);
    if (gp < 0) {
        return -1;
    }
    tiku_rp2350_gpio_init_output((uint8_t)gp);
    tiku_rp2350_gpio_set((uint8_t)gp, val);
    return 0;
}

int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin) {
    int8_t gp = gp_index(port, pin);
    if (gp < 0) {
        return -1;
    }
    /* If the pin isn't already an output, claim it as one (matches
     * the MSP430 driver's behaviour for `gpio 4 6 t`). */
    if (!(_RP2350_REG(RP2350_SIO_GPIO_OE) & (1U << gp))) {
        tiku_rp2350_gpio_init_output((uint8_t)gp);
    }
    tiku_rp2350_gpio_toggle((uint8_t)gp);
    return 0;
}

int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin) {
    int8_t gp = gp_index(port, pin);
    if (gp < 0) {
        return -1;
    }
    return (_RP2350_REG(RP2350_SIO_GPIO_IN) & (1U << gp)) ? 1 : 0;
}

int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin) {
    int8_t gp = gp_index(port, pin);
    if (gp < 0) {
        return -1;
    }
    return (_RP2350_REG(RP2350_SIO_GPIO_OE) & (1U << gp)) ? 1 : 0;
}
