/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - MSP430 software entropy source
 *
 * See tiku_trng_arch.h for the rationale.  The MSP430 has no hardware
 * RNG, so entropy is harvested from two independent physical sources on
 * the die and conditioned with SHA-256:
 *
 *   - Oscillator-ratio jitter: MCLK (DCO) and ACLK (XT1 32.768 kHz) are
 *     unlocked oscillators.  Each round counts MCLK loop iterations that
 *     elapse across exactly one ACLK tick; the low bits of that count
 *     drift with the two clocks' jitter (plus any interrupt latency that
 *     lands in the window).  This is the load-bearing source and gates
 *     the health check.
 *   - ADC thermal noise: the low bits of a 12-bit read of the internal
 *     temperature sensor.
 *
 * A feedback delay whose length depends on the previous round's noise
 * perturbs the ACLK sampling phase so successive rounds decorrelate.
 * POOL_ROUNDS rounds feed one SHA-256 block; the previous block's digest
 * is chained in so long reads keep advancing.  If the jitter source is
 * dead (count never varies -> stopped/absent timer) the read fails
 * closed: TLS then aborts rather than using predictable bytes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trng_arch.h"

#include <msp430.h>
#include <string.h>

#include <interfaces/adc/tiku_adc.h>
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>

/*
 * Rounds hashed per 32-byte output block.  Each round yields only a
 * handful of unpredictable bits, so the count is deliberately large:
 * with a conservative ~1 bit/round the pool still carries >256 bits of
 * min-entropy into the SHA-256 conditioner.  At ~55 us/round this is
 * ~14 ms per block -- fine, since callers seed a DRBG from it once.
 */
#define TRNG_POOL_ROUNDS   256u
#define TRNG_BLOCK_BYTES   32u   /* SHA-256 digest size */

/* Upper bound on the MCLK spin so a stopped ACLK can't wedge us: 2^16
 * DCO iterations ~= 8 ms with no ACLK edge means the timer is dead. */
#define TRNG_SPIN_LIMIT    0xFFFFu

static uint8_t trng_ready;                       /* init done            */
static uint8_t trng_chain[TRNG_BLOCK_BYTES];     /* inter-block chaining */

/*---------------------------------------------------------------------------*/

void
tiku_trng_arch_init(void)
{
    if (trng_ready) {
        return;
    }
#ifdef TIKU_BOARD_ADC_AVAILABLE
    {
        /* 12-bit conversions off AVCC.  The temperature sensor is a
         * high-impedance, noisy channel: we want the LSBs, not accuracy. */
        tiku_adc_config_t cfg;
        cfg.resolution = TIKU_ADC_RES_12BIT;
        cfg.reference  = TIKU_ADC_REF_AVCC;
        (void)tiku_adc_init(&cfg);
    }
#endif
    trng_ready = 1u;
}

/*---------------------------------------------------------------------------*/

/*
 * Harvest and condition one 32-byte block into @p out.  Returns
 * TIKU_TRNG_OK, or TIKU_TRNG_ERR_TIMEOUT if the jitter source never
 * varied (dead timer) -- in which case @p out is not written.
 */
static int
trng_block(uint8_t out[TRNG_BLOCK_BYTES])
{
    tiku_kits_crypto_sha256_ctx_t sha;
    uint16_t i;
    uint16_t cnt_first = 0u;
    uint8_t  cnt_varied = 0u;
    uint8_t  perturb = 0u;

    tiku_kits_crypto_sha256_init(&sha);
    /* Chain the previous digest so blocks differ and to carry forward
     * whatever entropy the last call accumulated. */
    (void)tiku_kits_crypto_sha256_update(&sha, trng_chain, sizeof trng_chain);

    for (i = 0u; i < TRNG_POOL_ROUNDS; i++) {
        uint16_t t0, cnt, tnoise;
        uint16_t adc = 0u;
        uint8_t  sample[4];

        /* --- oscillator-ratio jitter: MCLK iterations per ACLK tick --- */
        t0  = TA0R;
        cnt = 0u;
        while (TA0R == t0) {
            if (++cnt >= TRNG_SPIN_LIMIT) {   /* ACLK not advancing */
                break;
            }
        }
        tnoise = TA0R;                        /* async phase snapshot     */

        if (i == 0u) {
            cnt_first = cnt;
        } else if (cnt != cnt_first) {
            cnt_varied = 1u;
        }

        /* --- ADC thermal noise (LSBs) --- */
#ifdef TIKU_BOARD_ADC_AVAILABLE
        (void)tiku_adc_read(TIKU_ADC_CH_TEMP, &adc);
#endif

        sample[0] = (uint8_t)cnt;
        sample[1] = (uint8_t)(cnt >> 8);
        sample[2] = (uint8_t)(adc ^ tnoise);
        sample[3] = (uint8_t)((adc ^ tnoise) >> 8);
        (void)tiku_kits_crypto_sha256_update(&sha, sample, sizeof sample);

        /* Feedback: perturb the next ACLK sampling phase by a
         * noise-dependent number of cycles so rounds decorrelate. */
        perturb = (uint8_t)((cnt ^ adc ^ perturb) & 0x0Fu);
        {
            volatile uint8_t d = perturb;
            while (d--) {
                __no_operation();
            }
        }
    }

    /* Health test: the jitter source MUST have moved.  A count that is
     * constant for every round means ACLK is stopped or the loop was
     * fully deterministic -- either way there is no entropy, so refuse
     * rather than emit conditioned constants. */
    if (!cnt_varied) {
        return TIKU_TRNG_ERR_TIMEOUT;
    }

    (void)tiku_kits_crypto_sha256_final(&sha, out);
    memcpy(trng_chain, out, sizeof trng_chain);
    return TIKU_TRNG_OK;
}

/*---------------------------------------------------------------------------*/

int
tiku_trng_arch_read_bytes(uint8_t *buf, size_t len)
{
    uint8_t block[TRNG_BLOCK_BYTES];
    size_t  off = 0u;

    if (buf == (uint8_t *)0 || len == 0u) {
        return TIKU_TRNG_ERR_INVALID;
    }
    tiku_trng_arch_init();

    while (off < len) {
        size_t chunk = len - off;
        int    rc    = trng_block(block);
        if (rc != TIKU_TRNG_OK) {
            /* Fail closed: zero what we've written so a caller that
             * ignores the return (the void TLS RNG adapter) gets an
             * invalid all-zero key and the handshake aborts at the peer,
             * rather than leaking stack or using predictable bytes. */
            memset(buf, 0, len);
            memset(block, 0, sizeof block);
            return rc;
        }
        if (chunk > TRNG_BLOCK_BYTES) {
            chunk = TRNG_BLOCK_BYTES;
        }
        memcpy(buf + off, block, chunk);
        off += chunk;
    }

    memset(block, 0, sizeof block);          /* wipe transient entropy */
    return TIKU_TRNG_OK;
}

/*---------------------------------------------------------------------------*/

int
tiku_trng_arch_read_u32(uint32_t *out)
{
    uint8_t b[4];
    int rc;

    if (out == (uint32_t *)0) {
        return TIKU_TRNG_ERR_INVALID;
    }
    rc = tiku_trng_arch_read_bytes(b, sizeof b);
    if (rc != TIKU_TRNG_OK) {
        return rc;
    }
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return TIKU_TRNG_OK;
}
