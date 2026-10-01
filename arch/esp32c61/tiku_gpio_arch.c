/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - ESP32-C61 GPIO, and the board's addressable RGB LED.
 *
 * A pin is a GPIO when IO_MUX selects function 1 and the GPIO matrix routes
 * the GPIO_OUT bit (signal 256) to it; both are set on every init.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "tiku_gpio_arch.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_esp32c61_regs.h"

/** @brief IO_MUX: function GPIO, drive strength 2, input as asked. */
static void gpio_mux(uint8_t pin, int input) {
    uint32_t mux = TIKU_REG32(ESP32C61_IO_MUX_GPIO(pin));

    mux &= ~(ESP32C61_IO_MUX_MCU_SEL_MSK | ESP32C61_IO_MUX_DRV_MSK |
             ESP32C61_IO_MUX_FUN_IE);
    mux |= (ESP32C61_IO_MUX_FUNC_GPIO << ESP32C61_IO_MUX_MCU_SEL_POS) |
           (2UL << ESP32C61_IO_MUX_DRV_POS);
    if (input) {
        mux |= ESP32C61_IO_MUX_FUN_IE;
    }
    TIKU_REG32(ESP32C61_IO_MUX_GPIO(pin)) = mux;
}

void tiku_esp32c61_gpio_init_output(uint8_t pin) {
    if (pin >= ESP32C61_GPIO_COUNT) {
        return;
    }
    TIKU_REG32(ESP32C61_GPIO_OUT_W1TC) = 1UL << pin;
    gpio_mux(pin, 1);
    TIKU_REG32(ESP32C61_GPIO_OUT_SEL(pin)) = ESP32C61_GPIO_SIG_OUT;
    TIKU_REG32(ESP32C61_GPIO_ENABLE_W1TS) = 1UL << pin;
}

void tiku_esp32c61_gpio_set(uint8_t pin, uint8_t value) {
    if (pin < ESP32C61_GPIO_COUNT) {
        TIKU_REG32(value ? ESP32C61_GPIO_OUT_W1TS : ESP32C61_GPIO_OUT_W1TC) =
            1UL << pin;
    }
}

void tiku_esp32c61_gpio_toggle(uint8_t pin) {
    if (pin < ESP32C61_GPIO_COUNT) {
        tiku_esp32c61_gpio_set(pin,
            (TIKU_REG32(ESP32C61_GPIO_OUT) & (1UL << pin)) == 0UL);
    }
}

void tiku_esp32c61_rgb_set(uint8_t pin, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t hz = (uint32_t)tiku_cpu_esp32c61_clock_get_hz();
    uint32_t t0h = hz / 2857143UL;      /* 0.35 us */
    uint32_t t1h = hz / 1333333UL;      /* 0.75 us */
    uint32_t bit = hz / 800000UL;       /* 1.25 us */
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    uint32_t mask = 1UL << pin;
    uint32_t mie;

    if (pin >= ESP32C61_GPIO_COUNT) {
        return;
    }
    mie = ESP32C61_CSR_READ(mstatus) & ESP32C61_MSTATUS_MIE;
    ESP32C61_CSR_CLEAR(mstatus, ESP32C61_MSTATUS_MIE);
    for (int i = 23; i >= 0; i--) {
        uint32_t high = ((grb >> i) & 1UL) ? t1h : t0h;
        uint32_t start = ESP32C61_CSR_READ(mcycle);

        TIKU_REG32(ESP32C61_GPIO_OUT_W1TS) = mask;
        while ((uint32_t)(ESP32C61_CSR_READ(mcycle) - start) < high) {
        }
        TIKU_REG32(ESP32C61_GPIO_OUT_W1TC) = mask;
        while ((uint32_t)(ESP32C61_CSR_READ(mcycle) - start) < bit) {
        }
    }
    ESP32C61_CSR_SET(mstatus, mie);
}

void tiku_esp32c61_led_set(uint8_t pin, uint8_t channel, int on) {
    static uint8_t lit;                 /* one bit per colour */
    uint8_t bit = (uint8_t)(1U << channel);

    if (on < 0) {
        lit ^= bit;
    } else if (on) {
        lit |= bit;
    } else {
        lit &= (uint8_t)~bit;
    }
    /* Dim: it sits under the eye. */
    tiku_esp32c61_rgb_set(pin, (lit & 1U) ? 16U : 0U, (lit & 2U) ? 16U : 0U,
                          (lit & 4U) ? 16U : 0U);
}

/*---------------------------------------------------------------------------*/
/* KERNEL CONTRACT                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief The GPIO number a kernel (port, pin) names, or -1.
 *
 * Ports are 1-based banks of eight, as on the RP2350: port 1 pin 0 is GPIO0
 * and port 4 pin 5 is GPIO29, the last the part has.
 */
int tiku_esp32c61_gpio_num(uint8_t port, uint8_t pin) {
    unsigned n = (unsigned)(port - 1U) * 8U + pin;

    if (port < 1U || port > 4U || pin > 7U || n >= ESP32C61_GPIO_COUNT) {
        return -1;
    }
    return (int)n;
}

int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin) {
    int n = tiku_esp32c61_gpio_num(port, pin);

    if (n < 0) {
        return -1;
    }
    tiku_esp32c61_gpio_init_output((uint8_t)n);
    return 0;
}

int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin) {
    int n = tiku_esp32c61_gpio_num(port, pin);

    if (n < 0) {
        return -1;
    }
    TIKU_REG32(ESP32C61_GPIO_ENABLE_W1TC) = 1UL << n;
    gpio_mux((uint8_t)n, 1);
    return 0;
}

int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t val) {
    int n = tiku_esp32c61_gpio_num(port, pin);

    if (n < 0) {
        return -1;
    }
    tiku_esp32c61_gpio_set((uint8_t)n, val);
    return 0;
}

int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin) {
    int n = tiku_esp32c61_gpio_num(port, pin);

    if (n < 0) {
        return -1;
    }
    tiku_esp32c61_gpio_toggle((uint8_t)n);
    return 0;
}

int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin) {
    int n = tiku_esp32c61_gpio_num(port, pin);

    if (n < 0) {
        return -1;
    }
    return (TIKU_REG32(ESP32C61_GPIO_IN) >> n) & 1UL ? 1 : 0;
}

int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin) {
    int n = tiku_esp32c61_gpio_num(port, pin);

    if (n < 0) {
        return -1;
    }
    return (TIKU_REG32(ESP32C61_GPIO_ENABLE) >> n) & 1UL ? 1 : 0;
}
