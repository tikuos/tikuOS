/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - Ambiq CryptoCell-312 TRNG driver.
 *
 * A ring oscillator is sampled until 192 whitened bits fill the EHR.  On-die
 * health tests flag a bad run, which is treated as re-arm and retry; a dead source
 * surfaces as ERR_TIMEOUT so the TLS layer fails closed.  The CRYPTO domain is gated.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trng_arch.h"
#include <kernel/cpu/tiku_watchdog.h>   /* liveness kick during the gather */

#if defined(TIKU_DEVICE_APOLLO510)
#include "apollo510.h"
#else
#include "apollo4l.h"          /* apollo4l / apollo4p: register-compatible */
#endif

/*
 * TRNG private configuration.
 *
 * TRNG_ROSC_SEL selects the ring-oscillator length (TRNGCONFIG.RNDSRCSEL,
 * 0..3 = fastest..slowest).  Longer oscillators are better whitened and pass
 * autocorrelation more reliably, at the cost of fill latency.
 *
 * The slowest (3) plus 1000-cycle sampling took ~10 s to gather a
 * ClientHello's worth of entropy on Apollo510, long enough to stall the
 * TLS handshake mid-flight.  The 2nd-slowest ROSC at half the sample
 * count fills ~4x faster; the von Neumann debiaser and the
 * autocorr/CRNGT/VN health tests are the quality guarantee at any
 * setting, so this trades margin, not bias.
 *
 * TRNG_SAMPLE_COUNT is the rng_clk cycle count between bit samples
 * (SAMPLECNT1) -- higher means more decorrelation per bit.
 *
 * TRNG_CACHE_WORDS is the six EHR_DATA registers (192 bits per collection).
 * TRNG_SPIN_LIMIT bounds the wait for EHRVALID; TRNG_MAX_RETRIES bounds the
 * health-test re-arm loop.
 */
#define TRNG_ROSC_SEL        2u          /* RNDSRCSEL: 2nd-slowest, well-whitened */
#define TRNG_SAMPLE_COUNT    500u        /* SAMPLECNT1 rng_clk cycles/sample   */
#define TRNG_CACHE_WORDS     6u          /* EHR_DATA[0..5]                     */
#define TRNG_SPIN_LIMIT      4000000ul   /* ~tens of ms headroom at 96 MHz     */
#define TRNG_MAX_RETRIES     16u         /* health-test re-arm attempts        */
/** @} */

/* RNGISR bits this driver acts on. */
#define RNG_ISR_EHR_VALID    (1ul << 0)
#define RNG_ISR_AUTOCORRERR  (1ul << 1)
#define RNG_ISR_CRNGTERR     (1ul << 2)
#define RNG_ISR_VNERR        (1ul << 3)
#define RNG_ISR_ERRORS \
    (RNG_ISR_AUTOCORRERR | RNG_ISR_CRNGTERR | RNG_ISR_VNERR)

static uint32_t trng_cache[TRNG_CACHE_WORDS]; /**< drained EHR words      */
static uint8_t  trng_have;                    /**< words left in cache    */
static uint8_t  trng_ready;                   /**< init() completed       */

void tiku_trng_arch_init(void)
{
    volatile uint32_t spin = 0;
    if (trng_ready) {
        return;
    }
    /* Bring the CryptoCell power domain up and wait for it to settle. */
    PWRCTRL->DEVPWREN_b.PWRENCRYPTO = 1u;
    while (PWRCTRL->DEVPWRSTATUS_b.PWRSTCRYPTO == 0u &&
           ++spin < TRNG_SPIN_LIMIT) {
    }
    trng_have  = 0u;
    trng_ready = 1u;
}

/* Collect one 192-bit EHR block into the cache.  OK or TIMEOUT. */
static int trng_collect(void)
{
    unsigned retry;

    for (retry = 0u; retry < TRNG_MAX_RETRIES; retry++) {
        volatile uint32_t spin;
        uint32_t isr;

        /* Re-arm from a clean state. */
        CRYPTO->RNDSOURCEENABLE = 0u;
        CRYPTO->RNGICR          = 0xFFFFFFFFu;   /* clear status      */
        CRYPTO->RNGIMR          = 0xFFFFFFFFu;   /* mask IRQs (poll)  */
        CRYPTO->RNGCLKENABLE    = 1u;
        CRYPTO->RNGSWRESET      = 1u;            /* reset RNG core    */
        CRYPTO->RNGCLKENABLE    = 1u;            /* reset clears it   */
        CRYPTO->TRNGCONFIG      = TRNG_ROSC_SEL; /* ROSC; SOPSEL=TRNG */
        CRYPTO->SAMPLECNT1      = TRNG_SAMPLE_COUNT;
        CRYPTO->RNDSOURCEENABLE = 1u;            /* start sampling    */

        for (spin = 0u; spin < TRNG_SPIN_LIMIT; spin++) {
            /* The ring-oscillator gather blocks here for up to seconds per
             * re-arm (measured 2.5-16 s total on Apollo510 for a DRBG seed).
             * That is liveness, not a hang: kick periodically so neither the
             * hardware watchdog nor the check-in hang detector (which the
             * kick also feeds) resets the board mid-gather.  Masked to every
             * 64Ki spins -- ~100+ kicks/s, negligible poll-rate cost. */
            if ((spin & 0xFFFFul) == 0ul) {
                tiku_watchdog_kick();
            }
            isr = CRYPTO->RNGISR;
            if (isr & RNG_ISR_EHR_VALID) {
                trng_cache[0] = CRYPTO->EHRDATA0;
                trng_cache[1] = CRYPTO->EHRDATA1;
                trng_cache[2] = CRYPTO->EHRDATA2;
                trng_cache[3] = CRYPTO->EHRDATA3;
                trng_cache[4] = CRYPTO->EHRDATA4;
                trng_cache[5] = CRYPTO->EHRDATA5;
                CRYPTO->RNDSOURCEENABLE = 0u;
                CRYPTO->RNGICR          = 0xFFFFFFFFu;
                trng_have = TRNG_CACHE_WORDS;
                return TIKU_TRNG_OK;
            }
            if (isr & RNG_ISR_ERRORS) {
                break;                            /* health fail: re-arm */
            }
        }
    }
    CRYPTO->RNDSOURCEENABLE = 0u;
    return TIKU_TRNG_ERR_TIMEOUT;
}

int tiku_trng_arch_read_u32(uint32_t *out)
{
    if (out == (uint32_t *)0) {
        return TIKU_TRNG_ERR_INVALID;
    }
    if (!trng_ready) {
        tiku_trng_arch_init();
    }
    if (trng_have == 0u) {
        int rc = trng_collect();
        if (rc != TIKU_TRNG_OK) {
            return rc;
        }
    }
    *out = trng_cache[--trng_have];
    return TIKU_TRNG_OK;
}

int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len)
{
    size_t i = 0u;

    if (buf == (uint8_t *)0 && len != 0u) {
        return TIKU_TRNG_ERR_INVALID;
    }
    while (i < len) {
        uint32_t w;
        unsigned k;
        int rc = tiku_trng_arch_read_u32(&w);
        if (rc != TIKU_TRNG_OK) {
            return rc;
        }
        for (k = 0u; k < 4u && i < len; k++) {
            buf[i++] = (uint8_t)(w >> (8u * k));
        }
    }
    return TIKU_TRNG_OK;
}
