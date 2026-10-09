/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_gpio_arch.c - C5 GPIO matrix and IO mux, ESP-IDF 4d59230 register map.
 * USB, memory, strapping and RGB-LED pads cannot be remuxed through this API.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gpio_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32c5_regs.h"
#include <hal/tiku_gpio_irq_hal.h>
#include <hal/tiku_cpu.h>
#define GPIO_OUT 0x60091004u
#define GPIO_ENABLE 0x60091034u
#define GPIO_INPUT 0x60091064u
#define GPIO_MUX(pin) (0x60090000u + 4u * (pin))
#define GPIO_ROUTE(pin) (0x60091AD4u + 4u * (pin))

/** @brief Convert the kernel's one-based eight-pin ports to GPIO0..28. */
static int number(uint8_t port, uint8_t pin)
{
    unsigned n;
    if (port < 1 || port > 4 || pin > 7) { return -1; }
    n = (port - 1u) * 8u + pin;
    return n < 29 ? (int)n : -1;
}

/** @brief Test immutable reservations for the selected board. */
static int reserved(unsigned n)
{
#if !defined(TIKU_CONSOLE_JTAG)
    if (n == TIKU_BOARD_UART_TX_PIN || n == TIKU_BOARD_UART_RX_PIN) { return 1; }
#endif
    return (TIKU_BOARD_GPIO_RESERVED & (1u << n)) != 0;
}

int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin)
{
    static const uint8_t input_ranges[][2] = {
        {0, 0}, {6, 17}, {27, 35}, {41, 43}, {46, 66},
        {70, 70}, {74, 92}, {97, 116}
    };
    const uint32_t alternate = (15u << 2) | (1u << 7) | (7u << 10) |
                               (3u << 13) | (15u << 15) | (7u << 20);
    int n = number(port, pin);
    uint32_t mux, route;
    unsigned range, signal;
    if (n < 0) { return -1; }
    if (reserved((unsigned)n)) { return 1; }
    mux = (TIKU_C5_REG_READ(GPIO_MUX(n)) >> 12) & 7u;
    route = TIKU_C5_REG_READ(GPIO_ROUTE(n)) & 511u;
    if (mux > 1u || (mux == 0u && (alternate & (1u << n))) || route != 256u) {
        return 1;
    }
    for (range = 0; range < sizeof(input_ranges) / sizeof(input_ranges[0]); range++) {
        for (signal = input_ranges[range][0]; signal <= input_ranges[range][1]; signal++) {
            uint32_t input = TIKU_C5_REG_READ(0x600912D4u + 4u * signal);
            if ((input & 0x17Fu) == (0x100u | (unsigned)n)) { return 1; }
        }
    }
    return 0;
}

/** @brief Route a pad through software GPIO while preserving its pulls and drive strength. */
static void route_gpio(unsigned n)
{
    TIKU_C5_REG_WRITE(GPIO_MUX(n), (TIKU_C5_REG_READ(GPIO_MUX(n)) & ~(7u << 12)) |
                      (1u << 12) | (1u << 9));
    TIKU_C5_REG_WRITE(GPIO_ROUTE(n), 256u);
    TIKU_C5_REG_WRITE(0x600910D4u + 4u * n,
                      TIKU_C5_REG_READ(0x600910D4u + 4u * n) & ~(1u << 2));
}
int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t value)
{
    int n = number(port, pin);
    uint32_t state;
    if (n < 0 || reserved((unsigned)n)) { return -1; }
    state = TIKU_C5_IRQ_SAVE();
    if (tiku_gpio_arch_is_peripheral(port, pin) != 0) {
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    TIKU_C5_REG_WRITE(value ? GPIO_OUT + 4u : GPIO_OUT + 8u, 1u << n);
    route_gpio((unsigned)n);
    TIKU_C5_REG_WRITE(GPIO_ENABLE + 4u, 1u << n);
    TIKU_C5_IRQ_RESTORE(state);
    return 0;
}
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_write(port, pin, 0);
}
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    uint32_t state;
    if (n < 0 || reserved((unsigned)n)) { return -1; }
    state = TIKU_C5_IRQ_SAVE();
    if (tiku_gpio_arch_is_peripheral(port, pin) != 0) {
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    TIKU_C5_REG_WRITE(GPIO_ENABLE + 8u, 1u << n);
    route_gpio((unsigned)n);
    TIKU_C5_IRQ_RESTORE(state);
    return 0;
}
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    return n < 0 ? -1 : (int8_t)((TIKU_C5_REG_READ(GPIO_INPUT) >> n) & 1u);
}
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    return n < 0 ? -1 : (int8_t)((TIKU_C5_REG_READ(GPIO_ENABLE) >> n) & 1u);
}
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    int8_t result;
    uint32_t state;
    if (n < 0 || reserved((unsigned)n)) { return -1; }
    state = TIKU_C5_IRQ_SAVE();
    result = tiku_gpio_arch_write(port, pin, !(TIKU_C5_REG_READ(GPIO_OUT) & (1u << n)));
    TIKU_C5_IRQ_RESTORE(state);
    return result;
}

void tiku_c5_led_init(void)
{
    unsigned n = TIKU_BOARD_RGB_LED_PIN;
    uint32_t state = TIKU_C5_IRQ_SAVE();
    TIKU_C5_REG_WRITE(GPIO_OUT + 8u, 1u << n);
    route_gpio(n);
    TIKU_C5_REG_WRITE(GPIO_ENABLE + 4u, 1u << n);
    TIKU_C5_IRQ_RESTORE(state);
}

/** @brief Read the wrapping CPU cycle counter that times the LED frame. */
#ifndef TIKU_C5_LED_CYCLES
static uint32_t led_cycles(void)
{
    uint32_t value;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(value));
    return value;
}
#define TIKU_C5_LED_CYCLES() led_cycles()
#endif

/**
 * @brief Send one GRB frame to the WS2812-class LED: each bit is a 1.25 us
 *        period, high for 0.35 us (0) or 0.75 us (1), timed in CPU cycles.
 */
static void led_frame(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t hz = (uint32_t)tiku_cpu_mclk_hz();
    uint32_t t0h = hz / 2857143u, t1h = hz / 1333333u, period = hz / 800000u;
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    uint32_t mask = 1u << TIKU_BOARD_RGB_LED_PIN, state, start;
    int i;

    state = TIKU_C5_IRQ_SAVE();
    for (i = 23; i >= 0; i--) {
        uint32_t high = ((grb >> i) & 1u) ? t1h : t0h;
        start = TIKU_C5_LED_CYCLES();
        TIKU_C5_REG_WRITE(GPIO_OUT + 4u, mask);
        while ((uint32_t)(TIKU_C5_LED_CYCLES() - start) < high) {
        }
        TIKU_C5_REG_WRITE(GPIO_OUT + 8u, mask);
        while ((uint32_t)(TIKU_C5_LED_CYCLES() - start) < period) {
        }
    }
    TIKU_C5_IRQ_RESTORE(state);
}

void tiku_c5_led_set(uint8_t channel, int on)
{
    static uint8_t lit;                 /* one bit per colour */
    uint8_t bit = (uint8_t)(1u << channel);

    if (channel > 2u) {
        return;
    }
    if (on < 0) {
        lit ^= bit;
    } else if (on) {
        lit |= bit;
    } else {
        lit &= (uint8_t)~bit;
    }
    led_frame((lit & 1u) ? 16u : 0u, (lit & 2u) ? 16u : 0u,
              (lit & 4u) ? 16u : 0u);
}

int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin, tiku_gpio_edge_t edge)
{
    (void)port; (void)pin; (void)edge;
    return -1;
}
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin)
{
    (void)port; (void)pin;
    return -1;
}
