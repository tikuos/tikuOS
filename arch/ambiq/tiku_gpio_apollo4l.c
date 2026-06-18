/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_apollo4l.c - Apollo4 Lite GPIO access (bare-metal)
 *
 * Mirrors arch/ambiq/tiku_gpio_arch.c (Apollo510). The Apollo4 Lite GPIO block
 * is register-compatible for the operations tikuOS uses: the per-pad PINCFG0[]
 * array has FNCSEL[2:0] (3=GPIO), INPEN[4], OUTCFG[9:8] (1=push-pull) at the
 * same positions, and the WTS0/WTC0/WT0/RD0 set/clear/toggle/read banks are
 * indexed by pad/32 exactly as on Apollo510. PADKEY unlock value is 0x73.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpio_arch.h"
#include "apollo4l.h"       /* CMSIS register defs (GPIO_Type/GPIO) -- register header only */

/** Pad-index upper bound for tikuOS (the GPIO register space spans pads 0-127). */
#define TIKU_AMBIQ_GPIO_NUM_PADS  128u

/**
 * @defgroup GPIO_PINCFG GPIO_PINCFGn field masks and values
 * @brief Bit-field constants for the Apollo4 Lite GPIO pad-configuration
 *        register (GPIO->PINCFG0[pad]).
 * @{
 */
#define TIKU_GPIO_FNCSEL_GPIO      3u           /**< FNCSEL[2:0] = GPIO       */
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

/*---------------------------------------------------------------------------*/
/* Raw-pad helpers (used by the board LED macros)                            */
/*---------------------------------------------------------------------------*/

/** @brief Configure a pad as a push-pull GPIO output. */
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

/** @brief Toggle a GPIO pad output via WT0. */
void tiku_ambiq_gpio_toggle(uint32_t pad) {
    (&GPIO->WT0)[pad >> 5] ^= (1u << (pad & 31u));
}

/*---------------------------------------------------------------------------*/
/* Shared (port,pin) API -- maps port N pin p -> pad (N-1)*8 + p             */
/*---------------------------------------------------------------------------*/

/** @brief Convert a (port, pin) pair to a pad index, or -1 if invalid. */
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

/** @brief Write a digital value to a (port, pin) GPIO output. */
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    tiku_ambiq_gpio_init_output(pad);
    tiku_ambiq_gpio_set(pad, val);
    return 0;
}

/** @brief Toggle a (port, pin) GPIO output. */
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
