/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - STM32N6 hardware random number generator.
 *
 * Polled reads from the RNG data register, with the seed-error recovery the
 * reference manual requires before a suspect word can be trusted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trng_arch.h"
#include "tiku_stm32n6_regs.h"

/* Bounded so a stalled generator returns an error rather than the caller. */
#define TRNG_SPINS      200000UL

/** @brief Whether the block has been brought up this boot. */
static uint8_t trng_ready;

void tiku_trng_arch_init(void) {
    TIKU_REG32(STM32N6_RCC_AHB3ENR) |= STM32N6_RCC_AHB3ENR_RNG;
    (void)TIKU_REG32(STM32N6_RCC_AHB3ENR);

    /* Conditioning reset latches the noise-source configuration; the reset
     * defaults are the values the reference manual recommends, so only the
     * pulse itself is needed. */
    TIKU_REG32(STM32N6_RNG_CR) &= ~STM32N6_RNG_CR_RNGEN;
    TIKU_REG32(STM32N6_RNG_CR) |= STM32N6_RNG_CR_CONDRST;
    TIKU_REG32(STM32N6_RNG_CR) &= ~STM32N6_RNG_CR_CONDRST;

    for (unsigned long spins = TRNG_SPINS; spins > 0UL; spins--) {
        if ((TIKU_REG32(STM32N6_RNG_CR) & STM32N6_RNG_CR_CONDRST) == 0UL) {
            break;
        }
    }

    TIKU_REG32(STM32N6_RNG_CR) |= STM32N6_RNG_CR_RNGEN;
    trng_ready = 1U;
}

/**
 * @brief Clear a latched seed or clock error and restart conditioning.
 *
 * A seed error means the words behind it are suspect, so the generator is
 * reconditioned rather than read again.
 */
static void trng_recover(void) {
    TIKU_REG32(STM32N6_RNG_SR) &= ~(STM32N6_RNG_SR_SEIS | STM32N6_RNG_SR_CEIS);
    trng_ready = 0U;
    tiku_trng_arch_init();
}

int tiku_trng_arch_read_u32(uint32_t *out) {
    if (out == NULL) {
        return TIKU_TRNG_ERR_INVALID;
    }
    if (!trng_ready) {
        tiku_trng_arch_init();
    }

    for (unsigned long spins = TRNG_SPINS; spins > 0UL; spins--) {
        uint32_t sr = TIKU_REG32(STM32N6_RNG_SR);

        if (sr & (STM32N6_RNG_SR_SEIS | STM32N6_RNG_SR_CEIS)) {
            trng_recover();
            continue;
        }
        if (sr & STM32N6_RNG_SR_DRDY) {
            *out = TIKU_REG32(STM32N6_RNG_DR);
            return TIKU_TRNG_OK;
        }
    }
    return TIKU_TRNG_ERR_TIMEOUT;
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
