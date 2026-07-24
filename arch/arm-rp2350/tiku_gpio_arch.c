/*
 * Tiku Operating System v0.06
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

/** @brief Highest GP index exposed on the Pico 2 W header (GP0..GP29). */
#define MAX_GP_PIN  29U

/**
 * @brief Map a (port, pin) tuple to a flat SIO GP index (0..MAX_GP_PIN).
 *
 * Ports 1-4, pins 0-7 yield GP indices 0-31.  Indices above MAX_GP_PIN
 * and invalid port/pin values are rejected.
 *
 * @param port  GPIO port number (1-based, 1..4).
 * @param pin   Bit position within the port (0..7).
 * @return      Flat GP index on success, -1 on out-of-range input.
 */
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

/**
 * @brief Initialise a GP pin as a push-pull SIO output, driven low.
 *
 * Configures PADS_BANK0 (4 mA, input-enable kept on so SIO_GPIO_IN
 * reflects the driven level), routes IO_BANK0 to SIO, clears the
 * output latch, then enables the output-enable bit via the SIO
 * dedicated SET register.  Silently ignores pins above MAX_GP_PIN.
 *
 * @param pin  GP pin number (0..MAX_GP_PIN).
 */
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
    /* Output low, then enable output drive. SIO does NOT use the
     * generic +0x2000/+0x3000 atomic SET/CLR aliases — those address
     * unmapped SoC space and are silently dropped. SIO has its own
     * adjacent *_SET / *_CLR / *_XOR registers (offset +8 / +16 / +24
     * from each base register) which are the only correct way to do
     * an atomic set/clear here. */
    _RP2350_REG(RP2350_SIO_GPIO_OUT_CLR) = (1U << pin);
    _RP2350_REG(RP2350_SIO_GPIO_OE_SET)  = (1U << pin);
}

/**
 * @brief Drive a GP output pin high or low via the SIO atomic registers.
 *
 * Uses SIO_GPIO_OUT_SET / SIO_GPIO_OUT_CLR so the operation is
 * atomic and does not disturb other pins.  Silently ignores pins
 * above MAX_GP_PIN.
 *
 * @param pin    GP pin number (0..MAX_GP_PIN).
 * @param value  Non-zero drives the pin high; zero drives it low.
 */
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

/**
 * @brief Toggle a GP output pin using the SIO XOR register.
 *
 * The SIO_GPIO_OUT_XOR write is atomic and does not disturb other
 * pins.  Silently ignores pins above MAX_GP_PIN.
 *
 * @param pin  GP pin number (0..MAX_GP_PIN).
 */
void tiku_rp2350_gpio_toggle(uint8_t pin) {
    if (pin > MAX_GP_PIN) {
        return;
    }
    _RP2350_REG(RP2350_SIO_GPIO_OUT_XOR) = (1U << pin);
}

/*---------------------------------------------------------------------------*/
/* HAL entry points                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief HAL: configure a (port, pin) as a push-pull output.
 *
 * Translates the port/pin tuple to a flat GP index and calls
 * tiku_rp2350_gpio_init_output().
 *
 * @param port  GPIO port number (1-based, 1..4).
 * @param pin   Bit position within the port (0..7).
 * @return      0 on success, -1 if the port/pin is out of range.
 */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin) {
    int8_t gp = gp_index(port, pin);
    if (gp < 0) {
        return -1;
    }
    tiku_rp2350_gpio_init_output((uint8_t)gp);
    return 0;
}

/**
 * @brief HAL: configure a (port, pin) as a Schmitt-triggered input.
 *
 * Routes the pad to SIO with input-enable, pull-up, and Schmitt
 * trigger active.  Clears the output-enable bit via SIO_GPIO_OE_CLR.
 *
 * @param port  GPIO port number (1-based, 1..4).
 * @param pin   Bit position within the port (0..7).
 * @return      0 on success, -1 if the port/pin is out of range.
 */
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
    /* Output disable. SIO uses its dedicated CLR register (see
     * tiku_rp2350_gpio_init_output for why the generic CLR alias
     * doesn't work for SIO). */
    _RP2350_REG(RP2350_SIO_GPIO_OE_CLR) = (1U << gp);
    return 0;
}

/**
 * @brief HAL: write a logic level to a (port, pin), claiming it as an
 *        output first if necessary.
 *
 * Calls tiku_rp2350_gpio_init_output() to ensure output mode, then
 * drives the pin to the requested level.
 *
 * @param port  GPIO port number (1-based, 1..4).
 * @param pin   Bit position within the port (0..7).
 * @param val   Non-zero drives the pin high; zero drives it low.
 * @return      0 on success, -1 if the port/pin is out of range.
 */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val) {
    int8_t gp = gp_index(port, pin);
    if (gp < 0) {
        return -1;
    }
    tiku_rp2350_gpio_init_output((uint8_t)gp);
    tiku_rp2350_gpio_set((uint8_t)gp, val);
    return 0;
}

/**
 * @brief HAL: toggle a (port, pin), claiming it as an output if needed.
 *
 * If the GP pin's output-enable bit is not already set it is
 * initialised as an output first (matching MSP430 driver behaviour),
 * then toggled via SIO_GPIO_OUT_XOR.
 *
 * @param port  GPIO port number (1-based, 1..4).
 * @param pin   Bit position within the port (0..7).
 * @return      0 on success, -1 if the port/pin is out of range.
 */
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

/**
 * @brief HAL: sample the current logic level of a (port, pin).
 *
 * Reads SIO_GPIO_IN.  For output pins, the pad input buffer is kept
 * enabled by tiku_rp2350_gpio_init_output() so the driven level is
 * reflected correctly.
 *
 * @param port  GPIO port number (1-based, 1..4).
 * @param pin   Bit position within the port (0..7).
 * @return      1 if the pin is high, 0 if low, -1 on out-of-range input.
 */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin) {
    int8_t gp = gp_index(port, pin);
    if (gp < 0) {
        return -1;
    }
    return (_RP2350_REG(RP2350_SIO_GPIO_IN) & (1U << gp)) ? 1 : 0;
}

/**
 * @brief HAL: query the direction of a (port, pin).
 *
 * Checks the corresponding bit in SIO_GPIO_OE.
 *
 * @param port  GPIO port number (1-based, 1..4).
 * @param pin   Bit position within the port (0..7).
 * @return      1 if configured as output, 0 if input, -1 on out-of-range.
 */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin) {
    int8_t gp = gp_index(port, pin);
    if (gp < 0) {
        return -1;
    }
    return (_RP2350_REG(RP2350_SIO_GPIO_OE) & (1U << gp)) ? 1 : 0;
}
