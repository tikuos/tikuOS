/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - ESP32-C61 random words from the LP RNG.
 *
 * The data register is a generator that changes on every read; only with
 * sampling on does noise flow into it, so each word waits for fresh samples.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trng_arch.h"
#include "tiku_esp32c61_regs.h"

/* Samples mixed in per word: about 20 us at the ~1.8 MHz the counter runs. */
#define TRNG_FRESH      32U

/* Bounded so a stalled sampler returns an error rather than the caller. */
#define TRNG_SPINS      200000UL

static uint8_t trng_ready;

/** @brief The 8-bit sample counter. */
static uint32_t trng_count(void) {
    return (TIKU_REG32(ESP32C61_RNG_CFG) >> ESP32C61_RNG_CNT_POS) & 0xFFUL;
}

void tiku_trng_arch_init(void) {
    TIKU_REG32(ESP32C61_LPPERI_CLK_EN) |= ESP32C61_LPPERI_RNG_CLK;
    TIKU_REG32(ESP32C61_RNG_CFG) |= ESP32C61_RNG_SAMPLE_EN;
    trng_ready = 1U;
}

int tiku_trng_arch_read_u32(uint32_t *out) {
    uint32_t start, seen = 0UL;

    if (out == NULL) {
        return TIKU_TRNG_ERR_INVALID;
    }
    if (!trng_ready) {
        tiku_trng_arch_init();
    }
    /* No fresh samples means no noise in the word, and a word without noise
     * is a counter, not a random number: refuse rather than return it. */
    start = trng_count();
    for (unsigned long spins = TRNG_SPINS; spins > 0UL; spins--) {
        seen = (trng_count() - start) & 0xFFUL;
        if (seen >= TRNG_FRESH) {
            *out = TIKU_REG32(ESP32C61_RNG_DATA);
            return TIKU_TRNG_OK;
        }
    }
    return TIKU_TRNG_ERR_NOT_READY;
}

int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len) {
    if (buf == NULL) {
        return TIKU_TRNG_ERR_INVALID;
    }
    while (len > 0U) {
        uint32_t word;
        int rc = tiku_trng_arch_read_u32(&word);

        if (rc != TIKU_TRNG_OK) {
            return rc;
        }
        /* Spend all four bytes before asking for another word. */
        for (unsigned i = 0U; i < 4U && len > 0U; i++) {
            *buf++ = (uint8_t)(word & 0xFFU);
            word >>= 8;
            len--;
        }
    }
    return TIKU_TRNG_OK;
}
