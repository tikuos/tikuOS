/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - Apollo 510 GPIO access (bare-metal)
 *
 * Direct register access via the CMSIS device header (GPIO_Type) — no
 * am_hal calls, so the GPIO path no longer pulls libam_hal (de-SDK stage).
 *
 *   - pad config:  PADKEY unlock -> GPIO->PINCFG[pad] -> PADKEY relock
 *     (FNCSEL[3:0]=3 GPIO, INPEN[4], OUTCFG[9:8]=1 push-pull)
 *   - output:      GPIO->WTS0/WTC0/WT0 (set / clear / toggle), pad/32 indexed
 *   - input:       GPIO->RD0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpio_arch.h"
#include "apollo510.h"       /* CMSIS register defs (GPIO_Type/GPIO) -- register header only */

/* Apollo510 total GPIO pads (was AM_HAL_GPIO_MAX_PADS == AM_HAL_PIN_TOTAL_GPIOS). */
#define TIKU_AMBIQ_GPIO_NUM_PADS  224u

/* GPIO_PINCFGn fields (apollo510.h). */
#define TIKU_GPIO_FNCSEL_GPIO      3u           /* FNCSEL[3:0] = GPIO       */
#define TIKU_GPIO_INPEN            (1u << 4)    /* INPEN[4]                 */
#define TIKU_GPIO_OUTCFG_PUSHPULL  (1u << 8)    /* OUTCFG[9:8] = 1 push-pull*/
#define TIKU_GPIO_OUTCFG_MSK       (3u << 8)
#define TIKU_GPIO_PADKEY_UNLOCK    0x73u        /* GPIO_PADKEY_PADKEY_Key   */

static inline void pad_config(uint32_t pad, uint32_t cfg) {
    GPIO->PADKEY = TIKU_GPIO_PADKEY_UNLOCK;
    (&GPIO->PINCFG0)[pad] = cfg;
    GPIO->PADKEY = 0u;
}

/*---------------------------------------------------------------------------*/
/* Raw-pad helpers (used by the board LED macros)                            */
/*---------------------------------------------------------------------------*/

void tiku_ambiq_gpio_init_output(uint32_t pad) {
    pad_config(pad, TIKU_GPIO_FNCSEL_GPIO | TIKU_GPIO_OUTCFG_PUSHPULL);
}

void tiku_ambiq_gpio_set(uint32_t pad, uint8_t value) {
    uint32_t mask = (1u << (pad & 31u));
    if (value) {
        (&GPIO->WTS0)[pad >> 5] = mask;
    } else {
        (&GPIO->WTC0)[pad >> 5] = mask;
    }
}

void tiku_ambiq_gpio_toggle(uint32_t pad) {
    (&GPIO->WT0)[pad >> 5] ^= (1u << (pad & 31u));
}

/*---------------------------------------------------------------------------*/
/* Shared (port,pin) API — maps port N pin p -> pad (N-1)*8 + p              */
/*---------------------------------------------------------------------------*/

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

int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    tiku_ambiq_gpio_init_output(pad);
    return 0;
}

int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    pad_config(pad, TIKU_GPIO_FNCSEL_GPIO | TIKU_GPIO_INPEN);
    return 0;
}

int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    tiku_ambiq_gpio_set(pad, val);
    return 0;
}

int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    tiku_ambiq_gpio_toggle(pad);
    return 0;
}

int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    return (int8_t)(((&GPIO->RD0)[pad >> 5] >> (pad & 31u)) & 1u);
}

int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    /* Output if the OUTCFG field is non-zero (push-pull / open-drain). */
    return (int8_t)(((&GPIO->PINCFG0)[pad] & TIKU_GPIO_OUTCFG_MSK) ? 1 : 0);
}
