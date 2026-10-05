/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.c - RA8P1 independent watchdog.
 *
 * IWDTCR accepts one write between reset and the first refresh (UM 29.3.2),
 * and only a reset stops the counter: the first arm sets the period and later
 * calls feed it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"
#include "tiku_ra8p1_regs.h"

/**
 * @brief The (CKS, TOPS) pairs the hardware offers, coarsest divider last.
 *
 * A period is div x count IWDTCLK ticks, the unit of the requested interval.
 * The table is not sorted by period; the first entry that covers a request
 * is used.
 */
static const struct {
    uint8_t  cks;       /**< IWDTCR.CKS code                        */
    uint8_t  tops;      /**< IWDTCR.TOPS code                       */
    uint16_t div;       /**< divider the CKS code selects           */
    uint16_t count;     /**< down-counter start the TOPS code gives */
} wdt_periods[] = {
    { 0x0U, 0U,   1U,  128U }, { 0x0U, 1U,   1U,  512U },
    { 0x0U, 2U,   1U, 1024U }, { 0x0U, 3U,   1U, 2048U },
    { 0x2U, 0U,  16U,  128U }, { 0x2U, 1U,  16U,  512U },
    { 0x2U, 2U,  16U, 1024U }, { 0x2U, 3U,  16U, 2048U },
    { 0x3U, 0U,  32U,  128U }, { 0x3U, 1U,  32U,  512U },
    { 0x3U, 2U,  32U, 1024U }, { 0x3U, 3U,  32U, 2048U },
    { 0x4U, 0U,  64U,  128U }, { 0x4U, 1U,  64U,  512U },
    { 0x4U, 2U,  64U, 1024U }, { 0x4U, 3U,  64U, 2048U },
    { 0xFU, 0U, 128U,  128U }, { 0xFU, 1U, 128U,  512U },
    { 0xFU, 2U, 128U, 1024U }, { 0xFU, 3U, 128U, 2048U },
    { 0x5U, 0U, 256U,  128U }, { 0x5U, 1U, 256U,  512U },
    { 0x5U, 2U, 256U, 1024U }, { 0x5U, 3U, 256U, 2048U },
};

#define WDT_NPERIODS  (sizeof wdt_periods / sizeof wdt_periods[0])

/** @brief What the callers last asked for, and the period in force. */
static struct {
    uint8_t             armed;      /**< IWDTCR has been written        */
    uint8_t             running;    /**< caller wants it counting       */
    uint8_t             paused;
    uint8_t             idx;        /**< wdt_periods entry in force     */
    tiku_wdt_clk_t      src;
    tiku_wdt_interval_t interval;
} wdt_state;

/** @brief Refresh the down-counter: 0x00 then 0xFF, in order (UM 29.2.1). */
static void wdt_refresh(void)
{
    TIKU_REG8(RA8P1_IWDT_RR) = 0x00U;
    TIKU_REG8(RA8P1_IWDT_RR) = 0xFFU;
}

void tiku_cpu_ra8p1_watchdog_off_arch(void)
{
    /* Only a reset stops the IWDT (UM 29).  Feed it once and record the
     * request: kicks are then ignored, and the part resets one period later
     * unless the watchdog is armed again. */
    if (wdt_state.armed) {
        wdt_refresh();
    }
    wdt_state.running = 0U;
    wdt_state.paused  = 0U;
}

void tiku_cpu_ra8p1_watchdog_on_arch(tiku_wdt_clk_t src,
                                     tiku_wdt_interval_t interval)
{
    uint32_t want = (interval == 0U) ? 1UL : (uint32_t)interval;
    unsigned i;

    wdt_state.src      = src;
    wdt_state.interval = interval;
    wdt_state.running  = 1U;
    wdt_state.paused   = 0U;

    if (wdt_state.armed) {
        /* IWDTCR is write-once until a reset: re-arming feeds the counter
         * and keeps the first call's period, which
         * tiku_cpu_ra8p1_watchdog_period_ms() reports. */
        wdt_refresh();
        return;
    }

    /* The first entry whose period covers the request, which need not be
     * the shortest that does.  With none, the loop ends on the last entry,
     * the longest period (32 s). */
    for (i = 0; i < WDT_NPERIODS - 1U; i++) {
        uint32_t ticks = (uint32_t)wdt_periods[i].div * wdt_periods[i].count;
        if (ticks >= want) {
            break;
        }
    }

    TIKU_REG16(RA8P1_IWDT_CR) = (uint16_t)(
        RA8P1_IWDT_CR_CKS(wdt_periods[i].cks) |
        RA8P1_IWDT_CR_TOPS(wdt_periods[i].tops) |
        RA8P1_IWDT_CR_RPES_NONE | RA8P1_IWDT_CR_RPSS_NONE);

    wdt_state.idx   = (uint8_t)i;
    wdt_state.armed = 1U;

    /* The refresh starts the counter in register-start mode and locks
     * IWDTCR, so the IWDTCR write must come first. */
    wdt_refresh();
}

void tiku_cpu_ra8p1_watchdog_pause_arch(void)
{
    /* The counter cannot be halted; a refresh gives the section that
     * follows one full period. */
    if (wdt_state.armed) {
        wdt_refresh();
    }
    wdt_state.paused = 1U;
}

void tiku_cpu_ra8p1_watchdog_resume_arch(int kick_on_resume)
{
    if (wdt_state.armed && kick_on_resume) {
        wdt_refresh();
    }
    wdt_state.paused = 0U;
}

void tiku_cpu_ra8p1_watchdog_kick_arch(void)
{
    if (wdt_state.armed && wdt_state.running) {
        wdt_refresh();
    }
}

uint32_t tiku_cpu_ra8p1_watchdog_period_ms(void)
{
    uint32_t ticks;

    if (!wdt_state.armed) {
        return 0UL;
    }
    ticks = (uint32_t)wdt_periods[wdt_state.idx].div *
            wdt_periods[wdt_state.idx].count;
    return (ticks * 1000UL) / RA8P1_IWDTCLK_HZ;
}
