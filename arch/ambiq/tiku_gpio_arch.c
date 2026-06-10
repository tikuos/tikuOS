/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - Apollo 510 GPIO access
 *
 * Hybrid bring-up: pad configuration goes through am_hal_gpio_pinconfig
 * and the am_hal_gpio_output_* register macros (tagged @ambiq-sdk). The
 * pin-config struct is built explicitly from its bitfields, so the
 * de-SDK pass only has to replace am_hal_gpio_pinconfig with a direct
 * write to the pad's GPIOCFG register.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "am_mcu_apollo.h"   /* @ambiq-sdk: am_hal_gpio_* */
#include "tiku_gpio_arch.h"

#include <string.h>

/*---------------------------------------------------------------------------*/
/* Raw-pad helpers (used by the board LED macros)                            */
/*---------------------------------------------------------------------------*/

void tiku_ambiq_gpio_init_output(uint32_t pad) {
    am_hal_gpio_pincfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.GP.cfg_b.uFuncSel  = 3;                                /* GPIO function */
    cfg.GP.cfg_b.eGPInput  = AM_HAL_GPIO_PIN_INPUT_NONE;
    cfg.GP.cfg_b.eGPOutCfg = AM_HAL_GPIO_PIN_OUTCFG_PUSHPULL;
    am_hal_gpio_pinconfig(pad, cfg);                          /* @ambiq-sdk */
}

void tiku_ambiq_gpio_set(uint32_t pad, uint8_t value) {
    if (value) {
        am_hal_gpio_output_set(pad);     /* @ambiq-sdk */
    } else {
        am_hal_gpio_output_clear(pad);   /* @ambiq-sdk */
    }
}

void tiku_ambiq_gpio_toggle(uint32_t pad) {
    am_hal_gpio_output_toggle(pad);      /* @ambiq-sdk */
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
    if (p >= AM_HAL_GPIO_MAX_PADS) {
        return -1;
    }
    *pad = p;
    return 0;
}

static void ambiq_pincfg_input(uint32_t pad) {
    am_hal_gpio_pincfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.GP.cfg_b.uFuncSel  = 3;                               /* GPIO function */
    cfg.GP.cfg_b.eGPInput  = AM_HAL_GPIO_PIN_INPUT_ENABLE;
    cfg.GP.cfg_b.eGPOutCfg = AM_HAL_GPIO_PIN_OUTCFG_DISABLE;
    am_hal_gpio_pinconfig(pad, cfg);                          /* @ambiq-sdk */
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
    ambiq_pincfg_input(pad);
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
    return (int8_t)(am_hal_gpio_input_read(pad) ? 1 : 0);    /* @ambiq-sdk */
}

int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin) {
    uint32_t pad;
    if (ambiq_pad_of(port, pin, &pad)) { return -1; }
    /* TODO(de-sdk): read the pad's output-enable state. Stub: input. */
    return 0;
}
