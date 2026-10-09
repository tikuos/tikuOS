/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_trng_arch.c - bounded C5 internal-SAR entropy acquisition, and the
 * RF-fed RNG register while the PHY owns the analog bus.
 * Register sequence: ESP-IDF 4d59230 bootloader_random_esp32c5.c,
 * C5 adc_ll.h, rng_ll.h and regi2c_impl.c (Apache-2.0).
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trng_arch.h"
#include "tiku_esp32c5_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_cpu_common.h"
#include "tiku_analog_arch.h"

#define C5_SAR         0x6000E000u
#define C5_SAR_BUS     0x60096088u
#define C5_SAR_CLOCK   0x6009608Cu
#define C5_SAR_DIV     0x60096148u
#define C5_ANA_CLOCK   0x600AF018u
#define C5_ANA         0x600AF800u
#define C5_ANA_SELECT  (C5_ANA + 0x1Cu)
#define C5_ANA_MUX     (C5_ANA + 0x20u)
#define C5_SAR_POWER   0x600B0158u
#define C5_RNG_CLOCK   0x600B2800u
#define C5_RNG_RESET   0x600B2804u
#define C5_RNG_CFG     0x600B2824u
#define C5_RNG_DATA    0x600B2828u
#define C5_TRNG_SPINS  200000u
#ifndef TIKU_C5_ENTROPY_DELAY
#define TIKU_C5_ENTROPY_DELAY(us) tiku_cpu_c5_delay_us(us)
#endif

static const uint32_t saved_regs[] = {
    C5_SAR_BUS, C5_SAR_CLOCK, C5_SAR_DIV, C5_ANA_CLOCK, C5_SAR_POWER,
    C5_ANA_SELECT, C5_RNG_CLOCK, C5_RNG_CFG,
    C5_SAR, C5_SAR + 4, C5_SAR + 0x18, C5_SAR + 0x1C
};
static const uint8_t analog_regs[] = { 0, 1, 3, 4, 7, 8 };

/** @brief Change a register field without modifying neighboring fields. */
static void field(uint32_t address, uint32_t mask, uint32_t value)
{
    TIKU_C5_REG_WRITE(address, (TIKU_C5_REG_READ(address) & ~mask) | value);
}

/** @brief Wait a bounded number of reads for the analog bus to become idle. */
static int analog_idle(uint32_t address)
{
    unsigned n;
    for (n = 0; n < C5_TRNG_SPINS; n++) {
        if (!(TIKU_C5_REG_READ(address) & (1u << 25))) { return 0; }
    }
    return -1;
}

/** @brief Transfer one internal SAR register; refuse a stalled analog bus. */
static int analog_transfer(uint8_t reg, uint8_t *value, int write)
{
    uint32_t address = C5_ANA +
        ((TIKU_C5_REG_READ(C5_ANA_MUX) & (1u << 11)) ? 0u : 4u);
    uint32_t command = 0x69u | ((uint32_t)reg << 8);
    if (analog_idle(address)) { return -1; }
    TIKU_C5_REG_WRITE(C5_ANA_SELECT, 0xFFFFFFu & ~(1u << 9));
    if (write) { command |= (1u << 24) | ((uint32_t)*value << 16); }
    TIKU_C5_REG_WRITE(address, command);
    if (analog_idle(address)) { return -1; }
    if (!write) { *value = (uint8_t)(TIKU_C5_REG_READ(address) >> 16); }
    return 0;
}

void tiku_trng_arch_init(void) { }

int tiku_trng_arch_read_bytes(uint8_t *out, size_t length)
{
    uint32_t saved[sizeof(saved_regs) / sizeof(saved_regs[0])];
    uint8_t analog[sizeof(analog_regs)], changed = 0;
    uint32_t state, word = 0, start;
    unsigned i, n;
    size_t offset;
    int status = TIKU_TRNG_ERR_NOT_READY;
    if (length > 256u || (out == NULL && length)) { return TIKU_TRNG_ERR_INVALID; }
    if (!length) { return TIKU_TRNG_OK; }
    for (offset = 0; offset < length; offset++) { out[offset] = 0; }
    state = TIKU_C5_IRQ_SAVE();
    /* With the PHY owning the analog bus the RNG is fed by RF noise, as in
     * the reference's esp_random path: read it one word per microsecond and
     * leave the SAR alone. */
    if (tiku_c5_analog_owner() == TIKU_C5_ANALOG_PHY) {
        for (offset = 0; offset < length; offset++) {
            if (!(offset & 3u)) {
                TIKU_C5_ENTROPY_DELAY(1);
                word = TIKU_C5_REG_READ(C5_RNG_DATA);
            }
            out[offset] = (uint8_t)word;
            word >>= 8;
        }
        TIKU_C5_IRQ_RESTORE(state);
        return TIKU_TRNG_OK;
    }
    /* Active ADC conversion, temperature sensing or RNG sampling is owned
     * elsewhere. Acquisition must not reset or reconfigure that owner. */
    if (tiku_c5_analog_owner() != TIKU_C5_ANALOG_NONE ||
        (TIKU_C5_REG_READ(C5_SAR) & 2u) ||
        (TIKU_C5_REG_READ(C5_SAR + 0x58) & (1u << 22)) ||
        (TIKU_C5_REG_READ(C5_SAR + 4) & (1u << 24)) ||
        (TIKU_C5_REG_READ(C5_SAR + 0x20) & (1u << 29)) ||
        (TIKU_C5_REG_READ(C5_RNG_CFG) & 1u) ||
        (TIKU_C5_REG_READ(C5_RNG_RESET) & (1u << 24)) ||
        tiku_c5_analog_acquire(TIKU_C5_ANALOG_ENTROPY) != 0) {
        TIKU_C5_IRQ_RESTORE(state);
        return status;
    }
    for (i = 0; i < sizeof(saved) / sizeof(saved[0]); i++) {
        saved[i] = TIKU_C5_REG_READ(saved_regs[i]);
    }
    field(C5_SAR_BUS, 4u, 4u);
    field(C5_SAR_CLOCK, 0x7FFFFFu, 1u << 22);
    field(C5_SAR_POWER, 1u << 26, 0);
    TIKU_C5_ENTROPY_DELAY(1);
    field(C5_SAR_POWER, (1u << 26) | (1u << 27), (1u << 26) | (1u << 27));
    field(C5_ANA_CLOCK, 4u, 4u);
    for (i = 0; i < sizeof(analog); i++) {
        if (analog_transfer(analog_regs[i], &analog[i], 0)) { goto cleanup; }
    }
    for (i = 0; i < sizeof(analog); i++) {
        uint8_t value;
        switch (analog_regs[i]) {
        case 0: case 3: value = 2150u & 255u; break;
        case 1: case 4: value = (analog[i] & 0xF0u) | (2150u >> 8); break;
        case 7: value = (analog[i] & ~3u) | (1u << 6); break;
        default: value = analog[i] | 5u; break;
        }
        changed = (uint8_t)(i + 1);
        if (analog_transfer(analog_regs[i], &value, 1)) { goto cleanup; }
    }
    /* SAR1 channel 7 and the reserved SAR2 channel 1 are internal sources. */
    field(C5_SAR + 0x18, 0xFFF000u, (31u << 18) | (39u << 12));
    field(C5_SAR, (7u << 15) | (1u << 6) | (3u << 27) | 3u,
          (1u << 15) | (1u << 6));
    field(C5_SAR_DIV, 255u << 8, 15u << 8);
    field(C5_SAR + 4, (4095u << 12) | (1u << 24), (200u << 12) | (1u << 24));
    field(C5_RNG_CLOCK, 1u << 24, 1u << 24);
    field(C5_RNG_CFG, 4095u, 4095u);
    for (offset = 0; offset < length; offset++) {
        if (!(offset & 3u)) {
            /* At 48 MHz / 15 / 200, 2.2 ms covers at least 32 SAR conversions. */
            TIKU_C5_ENTROPY_DELAY(2200);
            start = TIKU_C5_REG_READ(C5_RNG_CFG) >> 24;
            for (n = 0; n < C5_TRNG_SPINS; n++) {
                uint32_t count = TIKU_C5_REG_READ(C5_RNG_CFG) >> 24;
                if (((count - start) & 255u) >= 32u) { break; }
            }
            if (n == C5_TRNG_SPINS) { goto cleanup; }
            word = TIKU_C5_REG_READ(C5_RNG_DATA);
        }
        out[offset] = (uint8_t)word;
        word >>= 8;
    }
    status = TIKU_TRNG_OK;
cleanup:
    field(C5_RNG_CFG, 1u, 0);
    field(C5_SAR + 4, 1u << 24, 0);
    while (changed) {
        --changed;
        if (analog_transfer(analog_regs[changed], &analog[changed], 1)) {
            status = TIKU_TRNG_ERR_NOT_READY;
        }
    }
    for (i = sizeof(saved) / sizeof(saved[0]); i > 0; i--) {
        TIKU_C5_REG_WRITE(saved_regs[i - 1], saved[i - 1]);
    }
    (void)tiku_c5_analog_release(TIKU_C5_ANALOG_ENTROPY);
    TIKU_C5_IRQ_RESTORE(state);
    if (status != TIKU_TRNG_OK) {
        for (offset = 0; offset < length; offset++) { out[offset] = 0; }
    }
    return status;
}

int tiku_trng_arch_read_u32(uint32_t *out)
{
    if (out == NULL) { return TIKU_TRNG_ERR_INVALID; }
    return tiku_trng_arch_read_bytes((uint8_t *)out, sizeof(*out));
}
