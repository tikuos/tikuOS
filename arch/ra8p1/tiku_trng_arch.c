/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - RA8P1 software entropy source.
 *
 * The part's hardware generator sits inside the RSIP, which is reachable only
 * through a vendor library, so entropy comes from the CAC measuring MOCO
 * against LOCO -- two independent RC oscillators -- conditioned with SHA-256.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trng_arch.h"

#include <string.h>

#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_freq_boot_arch.h"
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/*
 * Rounds folded into each 32-byte block.  A single ratio moves across only a
 * handful of values, so a round contributes single-digit bits; the count is
 * set for margin over throughput.  Measurements are in kintsugi/.
 */
#define TRNG_POOL_ROUNDS    128u
#define TRNG_BLOCK_BYTES    32u          /* SHA-256 digest */

/* MOCO against LOCO: two independent RC oscillators.  The divider widens the
 * measurement window so more of the count's low bits are free to move. */
#define TRNG_TARGET         RA8P1_CAC_CLK_MOCO
#define TRNG_REFERENCE      RA8P1_CAC_CLK_LOCO
#define TRNG_REF_DIV        0u

/* DWT, already used by the bench and NPU paths; TRCENA gates the whole unit. */
#define TRNG_DEMCR          0xE000EDFCUL
#define TRNG_DEMCR_TRCENA   (1UL << 24)
#define TRNG_DWT_CTRL       0xE0001000UL
#define TRNG_DWT_CYCCNT     0xE0001004UL
#define TRNG_DWT_CYCCNTENA  (1UL << 0)

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

static uint8_t trng_ready;
static uint8_t trng_chain[TRNG_BLOCK_BYTES];   /* chains block to block */

/*---------------------------------------------------------------------------*/
/* PRIVATE                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief One ratio measurement; 0 when the CAC did not complete. */
static uint16_t
trng_ratio(void)
{
    return tiku_cpu_ra8p1_cac_measure(TRNG_TARGET, TRNG_REFERENCE,
                                      TRNG_REF_DIV);
}

/** @brief Free-running core cycle count, for the phase against LOCO. */
static uint32_t
trng_cycles(void)
{
    return TIKU_REG32(TRNG_DWT_CYCCNT);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC                                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_trng_arch_init(void)
{
    if (trng_ready) {
        return;
    }
    /* The cycle counter needs the whole trace unit enabled, not just its own
     * bit -- the NPU bring-up found this the hard way. */
    TIKU_REG32(TRNG_DEMCR)    |= TRNG_DEMCR_TRCENA;
    TIKU_REG32(TRNG_DWT_CTRL) |= TRNG_DWT_CYCCNTENA;

    memset(trng_chain, 0, sizeof trng_chain);
    trng_ready = 1u;
}

int
tiku_trng_arch_raw_counts(uint16_t *out, size_t n)
{
    size_t i;

    if (out == NULL || n == 0u) {
        return TIKU_TRNG_ERR_INVALID;
    }
    tiku_trng_arch_init();

    for (i = 0u; i < n; i++) {
        out[i] = trng_ratio();
        if (out[i] == 0u) {
            return TIKU_TRNG_ERR_TIMEOUT;   /* CAC never completed */
        }
    }
    return TIKU_TRNG_OK;
}

/**
 * @brief Condition one 32-byte block out of TRNG_POOL_ROUNDS measurements.
 *
 * @param out  Receives TRNG_BLOCK_BYTES
 * @return TIKU_TRNG_OK, or TIKU_TRNG_ERR_TIMEOUT when the source is still
 */
static int
trng_block(uint8_t *out)
{
    tiku_kits_crypto_sha256_ctx_t sha;
    uint16_t first = 0u;
    uint8_t  varied = 0u;
    uint32_t perturb = 0u;
    size_t   i;

    tiku_kits_crypto_sha256_init(&sha);
    /* Chain the previous block so one weak round cannot expose this one. */
    (void)tiku_kits_crypto_sha256_update(&sha, trng_chain, sizeof trng_chain);

    for (i = 0u; i < TRNG_POOL_ROUNDS; i++) {
        uint16_t cnt = trng_ratio();
        uint32_t cyc = trng_cycles();
        uint8_t  sample[6];

        if (cnt == 0u) {
            return TIKU_TRNG_ERR_TIMEOUT;   /* CAC stalled mid-pool */
        }
        if (i == 0u) {
            first = cnt;
        } else if (cnt != first) {
            varied = 1u;
        }

        sample[0] = (uint8_t)cnt;
        sample[1] = (uint8_t)(cnt >> 8);
        sample[2] = (uint8_t)cyc;
        sample[3] = (uint8_t)(cyc >> 8);
        sample[4] = (uint8_t)(cyc >> 16);
        sample[5] = (uint8_t)(cyc >> 24);
        (void)tiku_kits_crypto_sha256_update(&sha, sample, sizeof sample);

        /* Decorrelate rounds: spin a noise-dependent number of cycles so the
         * next measurement does not start at a fixed phase. */
        perturb = (perturb ^ cnt ^ cyc) & 0x1Fu;
        {
            volatile uint32_t d = perturb;
            while (d-- != 0u) {
                __asm__ volatile ("nop");
            }
        }
    }

    /*
     * Health test.  A ratio that never moved across the whole pool means the
     * two oscillators are locked, one is stopped, or the measurement is
     * returning a constant -- in every case there is no uncertainty to
     * condition, and hashing a constant would produce something that passes
     * every statistical eyeball while being entirely predictable.  Refuse.
     */
    if (!varied) {
        return TIKU_TRNG_ERR_TIMEOUT;
    }

    (void)tiku_kits_crypto_sha256_final(&sha, out);
    memcpy(trng_chain, out, sizeof trng_chain);
    return TIKU_TRNG_OK;
}

int
tiku_trng_arch_read_bytes(uint8_t *buf, size_t len)
{
    uint8_t block[TRNG_BLOCK_BYTES];
    size_t done = 0u;

    if (buf == NULL || len == 0u) {
        return TIKU_TRNG_ERR_INVALID;
    }
    tiku_trng_arch_init();

    while (done < len) {
        size_t take = len - done;
        int rc = trng_block(block);

        if (rc != TIKU_TRNG_OK) {
            return rc;
        }
        if (take > TRNG_BLOCK_BYTES) {
            take = TRNG_BLOCK_BYTES;
        }
        memcpy(buf + done, block, take);
        done += take;
    }
    return TIKU_TRNG_OK;
}

int
tiku_trng_arch_read_u32(uint32_t *out)
{
    uint8_t b[4];
    int rc;

    if (out == NULL) {
        return TIKU_TRNG_ERR_INVALID;
    }
    rc = tiku_trng_arch_read_bytes(b, sizeof b);
    if (rc != TIKU_TRNG_OK) {
        return rc;
    }
    *out = ((uint32_t)b[0]) | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return TIKU_TRNG_OK;
}
