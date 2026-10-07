/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_apollo4l.c - Apollo4 Lite GPIO access (bare-metal).
 *
 * Uses the registers the same way as the Apollo510 file, tiku_gpio_arch.c:
 * the PINCFG fields, PADKEY and the per-32-pad set, clear, toggle and read
 * banks match.  tiku_ambiq_gpio_pad_config() is not defined for this part.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpio_arch.h"
#include "apollo4l.h"       /* CMSIS register definitions */

/** PINCFG0..127 cover pads 0-104 and virtual pads 105-127. */
#define TIKU_AMBIQ_GPIO_NUM_PADS  128u

/**
 * @defgroup GPIO_PINCFG GPIO_PINCFGn field masks and values
 * @brief Bit-field constants for the Apollo4 Lite GPIO pad-configuration
 *        register (GPIO->PINCFG0[pad]).
 * @{
 */
#define TIKU_GPIO_FNCSEL_GPIO      3u           /**< FNCSEL[3:0] = GPIO       */
#define TIKU_GPIO_INPEN            (1u << 4)    /**< INPEN[4] input enable    */
#define TIKU_GPIO_OUTCFG_PUSHPULL  (1u << 8)    /**< OUTCFG[9:8] = push-pull  */
#define TIKU_GPIO_OUTCFG_MSK       (3u << 8)    /**< OUTCFG field mask        */
#define TIKU_GPIO_PADKEY_UNLOCK    0x73u        /**< GPIO_PADKEY unlock value */
/** @} */

/**
 * @brief Write a pad configuration register under the PADKEY lock.
 *
 * @param pad  Pad index (0 .. TIKU_AMBIQ_GPIO_NUM_PADS-1)
 * @param cfg  PINCFG register value to write
 */
static inline void pad_config(uint32_t pad, uint32_t cfg) {
    GPIO->PADKEY = TIKU_GPIO_PADKEY_UNLOCK;
    (&GPIO->PINCFG0)[pad] = cfg;
    GPIO->PADKEY = 0u;
}

/** @brief Write a pad configuration; ignore indices outside PINCFG0..127. */
void tiku_ambiq_gpio_pad_config(uint32_t pad, uint32_t cfg) {
    if (pad < TIKU_AMBIQ_GPIO_NUM_PADS) {
        pad_config(pad, cfg);
    }
}

/*---------------------------------------------------------------------------*/
/* RAW-PAD HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/** @brief Configure a pad as a push-pull GPIO output, input buffer on. */
void tiku_ambiq_gpio_init_output(uint32_t pad) {
    pad_config(pad, TIKU_GPIO_FNCSEL_GPIO | TIKU_GPIO_OUTCFG_PUSHPULL |
                    TIKU_GPIO_INPEN);
}

/** @brief Drive a GPIO pad high (non-zero) or low (zero) via WTS0/WTC0. */
void tiku_ambiq_gpio_set(uint32_t pad, uint8_t value) {
    uint32_t mask = (1u << (pad & 31u));
    if (value) {
        (&GPIO->WTS0)[pad >> 5] = mask;
    } else {
        (&GPIO->WTC0)[pad >> 5] = mask;
    }
}

/** @brief Toggle a GPIO pad output via WT0 (read-modify-write, not atomic). */
void tiku_ambiq_gpio_toggle(uint32_t pad) {
    (&GPIO->WT0)[pad >> 5] ^= (1u << (pad & 31u));
}

/*---------------------------------------------------------------------------*/
/* SHARED (PORT, PIN) API                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Store pad (port-1)*8 + pin in *pad and return 0; -1 if invalid. */
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

/** @brief Configure a (port, pin) GPIO as a push-pull output. */
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    tiku_ambiq_gpio_init_output(pad);
    return 0;
}

/** @brief Configure a (port, pin) GPIO as a digital input. */
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    pad_config(pad, TIKU_GPIO_FNCSEL_GPIO | TIKU_GPIO_INPEN);
    return 0;
}

/** @brief Configure a (port, pin) GPIO as an output and drive it. */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    tiku_ambiq_gpio_init_output(pad);
    tiku_ambiq_gpio_set(pad, val);
    return 0;
}

/** @brief Configure a (port, pin) GPIO as an output and invert its level. */
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    tiku_ambiq_gpio_init_output(pad);
    tiku_ambiq_gpio_toggle(pad);
    return 0;
}

/** @brief Read the digital input level of a (port, pin) GPIO. */
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    return (int8_t)(((&GPIO->RD0)[pad >> 5] >> (pad & 31u)) & 1u);
}

/** @brief Query the direction of a (port, pin) GPIO (1=output, 0=input). */
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    return (int8_t)(((&GPIO->PINCFG0)[pad] & TIKU_GPIO_OUTCFG_MSK) ? 1 : 0);
}

/** @brief Report whether a (port, pin) pad's FNCSEL selects a peripheral. */
int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    return (((&GPIO->PINCFG0)[pad] & GPIO_PINCFG0_FNCSEL0_Msk) !=
            TIKU_GPIO_FNCSEL_GPIO) ? 1 : 0;
}
