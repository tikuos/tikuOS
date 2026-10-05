/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - Apollo510 GPIO access (bare-metal).
 *
 * Registers are reached through the CMSIS device header, with no vendor HAL:
 * a PADKEY-bracketed PINCFG write configures a pad, and output, toggle and
 * input use the per-32-pad banks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpio_arch.h"
#include "apollo510.h"       /* CMSIS register definitions */

/** Total number of GPIO pads on Apollo510 (AM_HAL_GPIO_MAX_PADS) */
#define TIKU_AMBIQ_GPIO_NUM_PADS  224u

/**
 * @defgroup GPIO_PINCFG GPIO_PINCFGn field masks and values
 * @brief Bit-field constants for the Apollo510 GPIO pad-configuration
 *        register (GPIO->PINCFG[pad]).  See apollo510.h for the full
 *        register layout.
 * @{
 */
#define TIKU_GPIO_FNCSEL_GPIO      3u           /**< FNCSEL[3:0] = GPIO       */
#define TIKU_GPIO_INPEN            (1u << 4)    /**< INPEN[4] input enable    */
#define TIKU_GPIO_OUTCFG_PUSHPULL  (1u << 8)    /**< OUTCFG[9:8] = push-pull  */
#define TIKU_GPIO_OUTCFG_MSK       (3u << 8)    /**< OUTCFG field mask        */
#define TIKU_GPIO_PADKEY_UNLOCK    0x73u        /**< GPIO_PADKEY_PADKEY_Key   */
/** @} */

/**
 * @brief Write a pad configuration register under the PADKEY lock
 *
 * PINCFG takes writes only while PADKEY holds the unlock key, so this
 * unlocks, writes cfg to GPIO->PINCFG[pad] and relocks.  @p pad is not
 * range-checked.
 *
 * @param pad  Pad index (0 .. TIKU_AMBIQ_GPIO_NUM_PADS-1)
 * @param cfg  PINCFG register value to write
 */
static inline void pad_config(uint32_t pad, uint32_t cfg) {
    GPIO->PADKEY = TIKU_GPIO_PADKEY_UNLOCK;
    (&GPIO->PINCFG0)[pad] = cfg;
    GPIO->PADKEY = 0u;
}

/**
 * @brief Range-checked pad_config() for drivers that give pads to a
 *        peripheral; the caller composes the PINCFG value.
 *
 * @param pad  Pad index (0 .. TIKU_AMBIQ_GPIO_NUM_PADS-1); a larger index
 *             is ignored
 * @param cfg  Full PINCFG register value
 */
void tiku_ambiq_gpio_pad_config(uint32_t pad, uint32_t cfg) {
    if (pad < TIKU_AMBIQ_GPIO_NUM_PADS) {
        pad_config(pad, cfg);
    }
}

/*---------------------------------------------------------------------------*/
/* RAW-PAD HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure an Apollo510 pad as a push-pull GPIO output
 *
 * Sets FNCSEL=GPIO, OUTCFG=push-pull and INPEN via pad_config().
 *
 * @param pad  Pad index (0 .. TIKU_AMBIQ_GPIO_NUM_PADS-1)
 */
void tiku_ambiq_gpio_init_output(uint32_t pad) {
    /* INPEN with OUTCFG lets tiku_gpio_arch_read() return the driven level
     * from RD; an Ambiq pad's input buffer works while it drives. */
    pad_config(pad, TIKU_GPIO_FNCSEL_GPIO | TIKU_GPIO_OUTCFG_PUSHPULL |
                    TIKU_GPIO_INPEN);
}

/**
 * @brief Drive an Apollo510 GPIO pad high or low
 *
 * Uses the atomic WTS0 (set) or WTC0 (clear) registers, indexed by
 * pad/32. Bit position within the word is pad%32.
 *
 * @param pad    Pad index (0 .. TIKU_AMBIQ_GPIO_NUM_PADS-1)
 * @param value  Non-zero to drive high; zero to drive low
 */
void tiku_ambiq_gpio_set(uint32_t pad, uint8_t value) {
    uint32_t mask = (1u << (pad & 31u));
    if (value) {
        (&GPIO->WTS0)[pad >> 5] = mask;
    } else {
        (&GPIO->WTC0)[pad >> 5] = mask;
    }
}

/**
 * @brief Toggle an Apollo510 GPIO pad output
 *
 * XORs the pad's bit in the WT0 (output data) register, indexed by
 * pad/32.  The read-modify-write is not atomic.
 *
 * @param pad  Pad index (0 .. TIKU_AMBIQ_GPIO_NUM_PADS-1)
 */
void tiku_ambiq_gpio_toggle(uint32_t pad) {
    (&GPIO->WT0)[pad >> 5] ^= (1u << (pad & 31u));
}

/*---------------------------------------------------------------------------*/
/* SHARED (PORT, PIN) API                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Convert a (port, pin) pair to an Apollo510 pad index
 *
 * Maps port N pin p to pad (N-1)*8 + p. Returns -1 if port is 0, pin
 * exceeds 7, or the resulting pad index is out of range.
 *
 * @param port  1-based port number
 * @param pin   Pin index within the port (0-7)
 * @param pad   Output pad index on success
 * @return 0 on success, -1 if the port/pin combination is invalid
 */
static int ambiq_pad_of(uint8_t port, uint8_t pin, uint32_t *pad) {
    uint32_t p;
    if (port < 1 || pin > 7) {
        return -1;
    }
    p = (uint32_t)(port - 1) * 8u + pin;
    if (p >= TIKU_AMBIQ_GPIO_NUM_PADS) {
        return -1;
    }
    *pad = p;
    return 0;
}

/**
 * @brief Configure a (port, pin) GPIO as a push-pull output
 *
 * @param port  1-based port number
 * @param pin   Pin index within the port (0-7)
 * @return 0 on success, -1 if port/pin is invalid
 */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    tiku_ambiq_gpio_init_output(pad);
    return 0;
}

/**
 * @brief Configure a (port, pin) GPIO as a digital input
 *
 * Sets FNCSEL=GPIO and enables the input buffer (INPEN).
 *
 * @param port  1-based port number
 * @param pin   Pin index within the port (0-7)
 * @return 0 on success, -1 if port/pin is invalid
 */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    pad_config(pad, TIKU_GPIO_FNCSEL_GPIO | TIKU_GPIO_INPEN);
    return 0;
}

/**
 * @brief Configure a (port, pin) GPIO as an output and drive it
 *
 * @param port  1-based port number
 * @param pin   Pin index within the port (0-7)
 * @param val   Non-zero to drive high; zero to drive low
 * @return 0 on success, -1 if port/pin is invalid
 */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    /* A write makes the pad an output, as tiku_gpio_set() documents.  The
     * PINCFG write replaces any other pad setting, such as a pull or drive
     * strength. */
    tiku_ambiq_gpio_init_output(pad);
    tiku_ambiq_gpio_set(pad, val);
    return 0;
}

/**
 * @brief Configure a (port, pin) GPIO as an output and invert its level
 *
 * @param port  1-based port number
 * @param pin   Pin index within the port (0-7)
 * @return 0 on success, -1 if port/pin is invalid
 */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    tiku_ambiq_gpio_init_output(pad);   /* makes it an output, as write */
    tiku_ambiq_gpio_toggle(pad);
    return 0;
}

/**
 * @brief Read the digital input level of a (port, pin) GPIO
 *
 * Reads the corresponding bit from GPIO->RD0[pad/32].
 *
 * @param port  1-based port number
 * @param pin   Pin index within the port (0-7)
 * @return 0 or 1 for the pin logic level, -1 if port/pin is invalid
 */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    return (int8_t)(((&GPIO->RD0)[pad >> 5] >> (pad & 31u)) & 1u);
}

/**
 * @brief Query the direction of a (port, pin) GPIO
 *
 * Reads the OUTCFG field of GPIO->PINCFG[pad]: non-zero means the pad
 * is configured as an output (push-pull or open-drain); zero means input.
 *
 * @param port  1-based port number
 * @param pin   Pin index within the port (0-7)
 * @return 1 if configured as output, 0 if input, -1 if port/pin invalid
 */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    /* Output if the OUTCFG field is non-zero (push-pull / open-drain). */
    return (int8_t)(((&GPIO->PINCFG0)[pad] & TIKU_GPIO_OUTCFG_MSK) ? 1 : 0);
}

/**
 * @brief Report whether a (port, pin) pad is routed to a peripheral
 *
 * Reads the FNCSEL field of GPIO->PINCFG[pad]: any function other than GPIO
 * is a peripheral's.  The pad configuration is only read.
 *
 * @param port  1-based port number
 * @param pin   Pin index within the port (0-7)
 * @return 1 if routed to a peripheral, 0 for a GPIO pad, -1 if port/pin is
 *         invalid
 */
int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    return (((&GPIO->PINCFG0)[pad] & GPIO_PINCFG0_FNCSEL0_Msk) !=
            TIKU_GPIO_FNCSEL_GPIO) ? 1 : 0;
}
