/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - Ambiq Apollo4/5 CryptoCell-312 TRNG driver
 *
 * Hardware: the Arm CryptoCell-312 RNG sub-block inside the Apollo CRYPTO
 * peripheral (base 0x400C0000).  A ring oscillator (one of four selectable
 * lengths) is sampled every SAMPLECNT1 rng_clk cycles; once 192 bits are
 * whitened into EHR_DATA[0..5], RNGISR.EHRVALID asserts.  On-die health
 * tests (autocorrelation, CRNGT, Von Neumann) flag a bad sample run via the
 * RNGISR error bits, which we treat as "re-arm and try again".
 *
 * Collect sequence (CC312 TRM, matched to Ambiq's am_hal_entropy):
 *   1. RNGCLKENABLE = 1                  enable the RNG clock
 *   2. RNGSWRESET   = 1                  reset the RNG core ...
 *   3. RNGCLKENABLE = 1                  ... which clears the clock-enable
 *   4. RNGICR = all-1s                   clear stale status
 *   5. TRNGCONFIG = ROSC select          pick a ring-oscillator length
 *   6. SAMPLECNT1 = sample count         clocks between bit samples
 *   7. RNDSOURCEENABLE = 1               start sampling
 *   8. spin on RNGISR: EHRVALID -> read EHR_DATA[0..5]; error bit -> re-arm
 *   9. RNDSOURCEENABLE = 0; clear status
 *
 * Power: the CRYPTO domain is power-gated; tiku_trng_arch_init() raises
 * PWRCTRL.DEVPWREN.PWRENCRYPTO and waits for DEVPWRSTATUS.PWRSTCRYPTO.
 *
 * The driver fills the cache from hardware; callers never deal with EHR
 * ordering, valid bits, or ROSC timing.  A health-test failure or a dead
 * source surfaces as TIKU_TRNG_ERR_TIMEOUT (the TLS layer then fails closed).
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

/**
 * @defgroup trng_config TRNG private configuration
 * @{
 *
 * TRNG_ROSC_SEL selects the ring-oscillator length (TRNGCONFIG.RNDSRCSEL,
 * 0..3 = fastest..slowest).  The longer oscillators are better whitened and
 * pass the autocorrelation test more reliably at the cost of fill latency.
 * The slowest (3) + 1000-cycle sampling took ~10 s to gather a ClientHello's
 * worth of entropy on Apollo510 -- long enough to stall the TLS handshake
 * mid-flight -- so use the 2nd-slowest ROSC (2, still well-whitened) at half
 * the sample count (~4x faster fill).  The von Neumann debiaser + the
 * autocorr/CRNGT/VN health tests (re-arm on failure, below) are the quality
 * guarantee at any ROSC/sample setting, so this trades only margin, not bias.
 *
 * TRNG_SAMPLE_COUNT is the rng_clk cycle count between bit samples
 * (SAMPLECNT1) -- higher = more decorrelation per bit.
 *
 * TRNG_CACHE_WORDS = the six EHR_DATA registers (192 bits per collection).
 * TRNG_SPIN_LIMIT bounds the wait for EHRVALID; TRNG_MAX_RETRIES bounds the
 * health-test re-arm loop.
 */
#define TRNG_ROSC_SEL        2u          /* RNDSRCSEL: 2nd-slowest, well-whitened */
#define TRNG_SAMPLE_COUNT    500u        /* SAMPLECNT1 rng_clk cycles/sample   */
#define TRNG_CACHE_WORDS     6u          /* EHR_DATA[0..5]                     */
#define TRNG_SPIN_LIMIT      4000000ul   /* ~tens of ms headroom at 96 MHz     */
#define TRNG_MAX_RETRIES     16u         /* health-test re-arm attempts        */
/** @} */

/* RNGISR bits we care about. */
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
