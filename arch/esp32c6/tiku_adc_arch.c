/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - C6 SAR ADC1 ownership, ground trim and bounded one-shot I/O.
 *
 * Register sequence follows ESP-IDF4d59230 C6 adc_ll.h and regi2c_saradc.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_adc_arch.h"
#include "tiku_io_arch.h"
#include <hal/tiku_mem_hal.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <interfaces/gpio/tiku_gpio_owner.h>
#include <stddef.h>
#ifndef C6_ADC_PIN_MASK
#define C6_ADC_PIN_MASK 0u
#endif
#define SAR 0x6000e000u
#define ANA 0x600af800u
static const uint32_t registers[] = {0x60096080u, 0x60096084u, 0x600af018u, 0x600b0154u,
                                     ANA + 0x1c,  SAR,         SAR + 4,     SAR + 0x20};
static const uint8_t analog_registers[] = {0, 1, 2, 7};
static uint32_t saved[8], mux[7], enable;
static uint8_t analog_saved[4], changed, claimed, opened, attenuation, shift, poisoned;
static uint16_t initial_code;
static const char owner[] = "adc1";
/** @brief Change a digital register field while preserving neighboring bits. */
static void field(uint32_t at, uint32_t mask, uint32_t value)
{
    TIKU_C6_REG_WRITE(at, (TIKU_C6_REG_READ(at) & ~mask) | value);
}
/** @brief Wait for the analog bus with a fixed poll bound. */
static int analog_idle(uint32_t at)
{
    for (unsigned n = 0; n < 200000; n++)
        if (!(TIKU_C6_REG_READ(at) & (1u << 25)))
            return 0;
    return -1;
}
/** @brief Transfer a SAR analog register through its selected internal bus. */
static int analog(uint8_t reg, uint8_t *value, int write)
{
    uint32_t at = ANA + ((TIKU_C6_REG_READ(ANA + 0x20) & (1u << 11)) ? 0 : 4);
    if (analog_idle(at))
        return -1;
    TIKU_C6_REG_WRITE(ANA + 0x1c, 0xffffffu & ~(1u << 9));
    TIKU_C6_REG_WRITE(at, 0x69u | ((uint32_t)reg << 8) |
                              (write ? ((1u << 24) | ((uint32_t)*value << 16)) : 0));
    if (analog_idle(at))
        return -1;
    if (!write)
        *value = (uint8_t)(TIKU_C6_REG_READ(at) >> 16);
    return 0;
}
/** @brief Delay at 16 MHz with a bounded snapshot source. */
static int delay_us(unsigned us)
{
    uint64_t start, now;
    if (tiku_c6_systimer_read(&start))
        return -1;
    for (unsigned i = 0; i < 200000; i++) {
        if (tiku_c6_systimer_read(&now))
            return -1;
        if (now - start >= (uint64_t)us * 16u)
            return 0;
    }
    return -1;
}
/** @brief Restore the analog and digital state saved for this transaction. */
static int end(void)
{
    int result = 0;
    while (changed) {
        --changed;
        if (analog(analog_registers[changed], analog_saved + changed, 1))
            result = -1;
    }
    for (unsigned n = 8; n; n--)
        TIKU_C6_REG_WRITE(registers[n - 1], saved[n - 1]);
    if (result)
        poisoned = 1;
    return result;
}
/** @brief Acquire an idle SAR and preserve its power, clocks and analog state. */
static int begin(void)
{
    if (poisoned || TIKU_MEM_ARCH_IN_EXCEPTION() || (TIKU_C6_REG_READ(0x600a9814u) & 0x1e000u) ||
        (TIKU_C6_REG_READ(SAR) & 2u) ||
        (TIKU_C6_REG_READ(SAR + 4) & (1u << 24)) || (TIKU_C6_REG_READ(SAR + 0x20) & (7u << 29)) ||
        (TIKU_C6_REG_READ(SAR + 0x58) & (1u << 22)) || TIKU_C6_REG_READ(SAR + 0x40))
        return -1;
    for (unsigned n = 0; n < 8; n++)
        saved[n] = TIKU_C6_REG_READ(registers[n]);
    changed = 0;
    field(0x60096080u, 4u, 4u);
    field(0x60096084u, 0x7fffffu, (1u << 22) | (15u << 12));
    field(0x600b0154u, 1u << 26, 0);
    if (delay_us(1)) {
        end();
        return -1;
    }
    field(0x600b0154u, 3u << 26, 3u << 26);
    field(0x600af018u, 4u, 4u);
    for (unsigned n = 0; n < 4; n++) {
        if (analog(analog_registers[n], analog_saved + n, 0)) {
            end();
            return -1;
        }
        changed = (uint8_t)(n + 1);
    }
    uint8_t ref = (analog_saved[2] & ~0x70u) | 0x10u;
    if (analog(2, &ref, 1)) {
        end();
        return -1;
    }
    field(SAR, (255u << 7) | (3u << 27) | (1u << 6), (15u << 7) | (3u << 27) | (1u << 6));
    return 0;
}
/** @brief Program the12-bit SAR initial code. */
static int set_code(uint16_t code)
{
    uint8_t low = (uint8_t)code, high = (analog_saved[1] & 0xf0u) | ((code >> 8) & 15u);
    return analog(0, &low, 1) || analog(1, &high, 1) ? -1 : 0;
}
/** @brief Start one software-triggered conversion and await ADC1 completion. */
static int convert(uint8_t channel, uint16_t *out)
{
    uint32_t select = (1u << 31) | ((uint32_t)channel << 25) | ((uint32_t)attenuation << 23);
    TIKU_C6_REG_WRITE(SAR + 0x20, select);
    if (delay_us(50))
        return -1;
    TIKU_C6_REG_WRITE(SAR + 0x4c, 1u << 31);
    TIKU_C6_REG_WRITE(SAR + 0x20, select | (1u << 29));
    if (delay_us(3))
        return -1;
    TIKU_C6_REG_WRITE(SAR + 0x20, select);
    for (unsigned n = 0; n < 1000; n++) {
        if (TIKU_C6_REG_READ(SAR + 0x44) & (1u << 31)) {
            *out = (uint16_t)(TIKU_C6_REG_READ(SAR + 0x2c) & 4095u);
            return 0;
        }
        if (delay_us(1))
            break;
    }
    return -1;
}
/** @brief Measure the mean internal-ground transition code over ten searches. */
static int trim(void)
{
    uint8_t ground = analog_saved[3] | (1u << 5);
    if (analog(7, &ground, 1))
        return -1;
    uint32_t sum = 0, minimum = 4096, maximum = 0;
    for (unsigned repeat = 0; repeat < 10; repeat++) {
        unsigned low = 0, high = 4095;
        while (high > low) {
            unsigned code = (low + high) / 2;
            uint16_t raw;
            if (set_code(code) || convert(0, &raw))
                return -1;
            if (raw)
                low = code + 1;
            else
                high = code;
        }
        if (low < minimum)
            minimum = low;
        if (low > maximum)
            maximum = low;
        sum += low;
        tiku_watchdog_kick();
    }
    uint16_t check;
    if (set_code(4095) || convert(0, &check) || check)
        return -1;
    initial_code = (uint16_t)((sum - minimum - maximum + 4) / 8);
    return 0;
}
void tiku_adc_arch_close(void)
{
    uint32_t state = TIKU_C6_IRQ_SAVE();
    for (unsigned n = 0; n < 7; n++)
        if (claimed & (1u << n)) {
            TIKU_C6_REG_WRITE(0x60090004u + 4 * n, mux[n]);
            if (enable & (1u << n))
                TIKU_C6_REG_WRITE(0x60091024u, 1u << n);
            (void)tiku_gpio_release(n / 8u + 1u, n % 8u, owner);
        }
    claimed = opened = 0;
    initial_code = 0;
    TIKU_C6_IRQ_RESTORE(state);
}
int tiku_adc_arch_init(const tiku_adc_config_t *config)
{
    if (!config || config->resolution > 2 || config->reference > 3)
        return TIKU_ADC_ERR_PARAM;
    uint32_t state = TIKU_C6_IRQ_SAVE();
    tiku_adc_arch_close();
    static const uint8_t atten[] = {3, 1, 2, 3};
    attenuation = atten[config->reference];
    shift = (uint8_t)(4u - 2u * config->resolution);
    int result = begin();
    if (!result) {
        result = trim();
        if (end())
            result = -1;
    }
    opened = result == 0;
    TIKU_C6_IRQ_RESTORE(state);
    return result ? TIKU_ADC_ERR_TIMEOUT : 0;
}
int tiku_adc_arch_channel_init(uint8_t channel)
{
    if (!opened || channel > 6 || !(C6_ADC_PIN_MASK & (1u << channel)))
        return TIKU_ADC_ERR_PARAM;
    uint32_t state = TIKU_C6_IRQ_SAVE();
    if (!(claimed & (1u << channel))) {
        if (tiku_gpio_claim(1, channel, owner)) {
            TIKU_C6_IRQ_RESTORE(state);
            return TIKU_ADC_ERR_PARAM;
        }
        uint32_t at = 0x60090004u + 4 * channel;
        mux[channel] = TIKU_C6_REG_READ(at);
        if (TIKU_C6_REG_READ(0x60091020u) & (1u << channel))
            enable |= 1u << channel;
        else
            enable &= ~(1u << channel);
        TIKU_C6_REG_WRITE(0x60091028u, 1u << channel);
        TIKU_C6_REG_WRITE(at, (mux[channel] & ~((7u << 12) | (7u << 7))) | (1u << 12));
        claimed |= 1u << channel;
    }
    TIKU_C6_IRQ_RESTORE(state);
    return 0;
}
int tiku_adc_arch_read(uint8_t channel, uint16_t *value)
{
    if (value)
        *value = 0;
    if (!value || !opened || channel > 6 || !(claimed & (1u << channel)))
        return TIKU_ADC_ERR_PARAM;
    uint32_t state = TIKU_C6_IRQ_SAVE();
    int result = begin();
    if (!result) {
        uint8_t ground = analog_saved[3] & ~(1u << 5);
        uint16_t raw = 0;
        result = analog(7, &ground, 1) || set_code(initial_code) || convert(channel, &raw);
        if (end())
            result = -1;
        if (!result)
            *value = raw >> shift;
    }
    TIKU_C6_IRQ_RESTORE(state);
    return result ? TIKU_ADC_ERR_TIMEOUT : 0;
}
uint16_t tiku_c6_adc_init_code(void)
{
    return opened ? initial_code : 0;
}
