/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - Native P4 ECO2 SAR noise source and bounded RNG reads.
 *
 * Each read samples an internal SAR channel, then restores its registers.
 * Sequence: ESP-IDF 4d59230 bootloader_random_esp32p4.c and adc_ll.h.
 *
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trng_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_systimer_arch.h"

#define ADC_CTRL 0x500de000u
#define ADC_CTRL2 0x500de004u
#define ADC_PATTERN 0x500de018u
#define ADC_CLK_SRC 0x500e609cu
#define ADC_CLK_DIV 0x500e60a0u
#define ADC_RESET 0x500e60c8u
#define SAR_POWER 0x5011515cu
#define LP_ADC_CTRL 0x5012700cu
#define LP_ADC_MUX 0x50127010u
#define RNG_DATA 0x501101a4u

/** @brief Wait for the internal analog bus without unbounded ROM polling. */
static int analog_idle(void)
{
    unsigned i;
    for (i = 0; i < 4096u; i++)
        if (!(TIKU_P4_REG_READ(TIKU_P4_ANALOG_CTRL) & TIKU_P4_ANALOG_BUSY)) return 0;
    return -1;
}
/** @brief Read or write one SAR analog byte; a write verifies its readback. */
static int analog(unsigned reg, uint32_t *value, int write)
{
    uint32_t command = 0x69u | (reg << 8), expected = *value;
    if (analog_idle()) return -1;
    if (write) command |= TIKU_P4_ANALOG_WRITE | (*value << 16);
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CTRL, command);
    if (analog_idle()) return -1;
    if (write) {
        TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CTRL, 0x69u | (reg << 8));
        if (analog_idle()) return -1;
    }
    *value = (TIKU_P4_REG_READ(TIKU_P4_ANALOG_CTRL) >> 16) & 255u;
    return write && *value != expected ? -1 : 0;
}
/** @brief Wait using the crystal-derived timer, with a bounded polling count. */
static int delay_us(unsigned us)
{
    uint64_t start, now;
    unsigned tries;
    if (tiku_p4_systimer_read(&start)) return -1;
    for (tries = 0; tries < 1000000u; tries++) {
        if (tiku_p4_systimer_read(&now)) return -1;
        if (((now - start) & TIKU_P4_SYSTIMER_MASK) >= us * 16u) return 0;
    }
    return -1;
}

void tiku_trng_arch_init(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    /* The ROM may leave the entropy ADC clocked after downloading an image. */
    TIKU_P4_REG_WRITE(TIKU_P4_SOC_CLK2, TIKU_P4_REG_READ(TIKU_P4_SOC_CLK2) | 32u);
    TIKU_P4_REG_WRITE(ADC_CLK_DIV, TIKU_P4_REG_READ(ADC_CLK_DIV) | 1u);
    TIKU_P4_REG_WRITE(ADC_CTRL2, 0);
    TIKU_P4_REG_WRITE(ADC_RESET, TIKU_P4_REG_READ(ADC_RESET) | (1u << 10));
    TIKU_P4_REG_WRITE(ADC_RESET, TIKU_P4_REG_READ(ADC_RESET) & ~(1u << 10));
    TIKU_P4_REG_WRITE(ADC_CLK_DIV, TIKU_P4_REG_READ(ADC_CLK_DIV) & ~1u);
    TIKU_P4_REG_WRITE(TIKU_P4_SOC_CLK2, TIKU_P4_REG_READ(TIKU_P4_SOC_CLK2) & ~32u);
    TIKU_P4_IRQ_RESTORE(state);
}
int tiku_trng_arch_read_bytes(uint8_t *out, size_t length)
{
    static const uintptr_t registers[] = {
        TIKU_P4_SOC_CLK2, ADC_CLK_SRC, ADC_CLK_DIV, ADC_RESET,
        TIKU_P4_LP_CLK, SAR_POWER, TIKU_P4_ANALOG_CONF1, TIKU_P4_ANALOG_CONF2,
        LP_ADC_CTRL, LP_ADC_MUX
    };
    static const unsigned analog_regs[] = {0, 1, 9};
    uint32_t saved[10], bytes[3], value, previous = 0, state;
    unsigned i, captured = 0;
    size_t at;
    int rc = TIKU_TRNG_ERR_TIMEOUT;
    if (!out || !length || length > 256) return TIKU_TRNG_ERR_INVALID;
    for (at = 0; at < length; at++) out[at] = 0;
    if (tiku_p4_in_isr()) return TIKU_TRNG_ERR_NOT_READY;
    state = TIKU_P4_IRQ_SAVE();
    if ((TIKU_P4_REG_READ(TIKU_P4_SOC_CLK2) & 32u) ||
        (TIKU_P4_REG_READ(ADC_CLK_DIV) & 1u) || analog_idle()) {
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_TRNG_ERR_NOT_READY;
    }
    for (i = 0; i < 10; i++) saved[i] = TIKU_P4_REG_READ(registers[i]);
    TIKU_P4_REG_WRITE(TIKU_P4_LP_CLK, saved[4] | (1u << 27));
    TIKU_P4_REG_WRITE(SAR_POWER, saved[5] & ~(1u << 26));
    if (delay_us(1)) goto done;
    TIKU_P4_REG_WRITE(SAR_POWER, saved[5] | (3u << 26));
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CONF1, saved[6] & ~0xffffffu);
    TIKU_P4_REG_WRITE(TIKU_P4_ANALOG_CONF2, (saved[7] & ~0xffffffu) | 128u);
    for (i = 0; i < 3; i++) {
        bytes[i] = 0;
        if (analog(analog_regs[i], &bytes[i], 0)) goto done;
        captured++;
    }
    value = 2166u & 255u;
    if (analog(0, &value, 1)) goto done;
    value = (bytes[1] & ~15u) | (2166u >> 8);
    if (analog(1, &value, 1)) goto done;
    value = (bytes[2] & ~31u) | 16u;
    if (analog(9, &value, 1)) goto done;
    TIKU_P4_REG_WRITE(ADC_RESET, saved[3] | (1u << 10));
    TIKU_P4_REG_WRITE(ADC_RESET, saved[3] & ~(1u << 10));
    TIKU_P4_REG_WRITE(TIKU_P4_SOC_CLK2, saved[0] | 32u);
    TIKU_P4_REG_WRITE(ADC_CLK_SRC, saved[1] & ~(3u << 30));
    TIKU_P4_REG_WRITE(ADC_CLK_DIV, 1u);
    TIKU_P4_REG_WRITE(ADC_CTRL, (3u << 26) | (15u << 6) | 32u);
    /* Internal channel 10, attenuation 3, four identical six-bit patterns. */
    TIKU_P4_REG_WRITE(ADC_PATTERN, 0x2bu * 0x41041u);
    TIKU_P4_REG_WRITE(LP_ADC_CTRL, saved[8] | (1u << 18) | (1u << 31));
    TIKU_P4_REG_WRITE(LP_ADC_MUX, saved[9] | (1u << 31));
    TIKU_P4_REG_WRITE(ADC_CTRL2, (100u << 12) | (1u << 11) | (1u << 24));
    for (at = 0; at < length;) {
        /* At least 32 conversions between words at XTAL/(15*2*100). */
        if (delay_us(3000)) goto done;
        value = TIKU_P4_REG_READ(RNG_DATA);
        if (!value || value == UINT32_MAX || (at && value == previous)) goto done;
        previous = value;
        for (i = 0; i < 4 && at < length; i++, at++) out[at] = value >> (8u * i);
    }
    rc = TIKU_TRNG_OK;
done:
    if (TIKU_P4_REG_READ(ADC_CLK_DIV) & 1u) TIKU_P4_REG_WRITE(ADC_CTRL2, 0);
    for (i = 0; i < captured; i++)
        if (analog(analog_regs[i], &bytes[i], 1)) rc = TIKU_TRNG_ERR_TIMEOUT;
    for (i = 10; i > 0; i--) TIKU_P4_REG_WRITE(registers[i - 1], saved[i - 1]);
    if (rc != TIKU_TRNG_OK)
        for (at = 0; at < length; at++) out[at] = 0;
    TIKU_P4_IRQ_RESTORE(state);
    return rc;
}
int tiku_trng_arch_read_u32(uint32_t *out)
{
    return tiku_trng_arch_read_bytes((uint8_t *)out, sizeof *out);
}
