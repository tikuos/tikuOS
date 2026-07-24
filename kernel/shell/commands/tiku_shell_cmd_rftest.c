/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_rftest.c - "rftest" shell command: RF test carrier.
 *
 * Drives tiku_radio_arch_carrier_start/stop -- the bench signals every
 * radio bring-up needs: an unmodulated tone for antenna/matching work,
 * XO trim and conducted-power checks, and a modulated carrier for
 * occupied-bandwidth and eye-quality looks.
 *
 * The carrier OUTLIVES the command: "rftest cw 2440" returns to the
 * prompt with the radio still transmitting, and it keeps transmitting
 * until "rftest off".  That is what a spectrum-analyser session wants,
 * but it means the radio is unavailable to beacon/scan/BLE until it is
 * stopped, so every path here says so and "status" reports it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_rftest.h"

#include <string.h>
#include <kernel/shell/tiku_shell_config.h>   /* resolved command flag  */
#include <kernel/shell/tiku_shell_io.h>

/* The config header resolves this flag against TIKU_HAS_BLE_ADV, so a
 * build without the radio compiles this file to nothing -- no stub, no
 * table entry, no help line. */
#if TIKU_SHELL_CMD_RFTEST

#include <arch/nordic/tiku_radio_arch.h>
#include <arch/nordic/tiku_timer_arch.h>       /* TIKU_CLOCK_ARCH_SECOND */
#include <kernel/timers/tiku_clock.h>
#include <kernel/cpu/tiku_watchdog.h>

/* Band limits enforced by the arch layer; repeated here for messages. */
#define RFT_MHZ_MIN   2360u
#define RFT_MHZ_MAX   2500u

/** Frequency the running carrier was started on (for "status"). */
static uint16_t rft_mhz;
/** Non-zero if the running carrier is the modulated form. */
static uint8_t  rft_modulated;

/* Tiny non-negative decimal parser (no shell helper for this). */
static long rft_atoi(const char *s)
{
    long v = 0;
    if (s == (const char *)0) {
        return -1;
    }
    while (*s >= '0' && *s <= '9') {
        v = (v * 10) + (*s - '0');
        s++;
    }
    return (*s == '\0') ? v : -1;
}

/* Signed variant so a negative dBm step parses. */
static long rft_atoi_signed(const char *s, long dflt)
{
    if (s == (const char *)0) {
        return dflt;
    }
    if (*s == '-') {
        long v = rft_atoi(s + 1);
        return (v < 0) ? dflt : -v;
    }
    {
        long v = rft_atoi(s);
        return (v < 0) ? dflt : v;
    }
}

static tiku_radio_arch_phy_t rft_phy(const char *arg, const char **name)
{
    *name = "1M";
    if (arg == (const char *)0) {
        return TIKU_RADIO_PHY_1M;
    }
    if (strcmp(arg, "2m") == 0 || strcmp(arg, "2M") == 0) {
        *name = "2M";
        return TIKU_RADIO_PHY_2M;
    }
    if (strcmp(arg, "s8") == 0 || strcmp(arg, "S8") == 0) {
        *name = "Coded S8";
        return TIKU_RADIO_PHY_CODED_S8;
    }
    if (strcmp(arg, "s2") == 0 || strcmp(arg, "S2") == 0) {
        *name = "Coded S2";
        return TIKU_RADIO_PHY_CODED_S2;
    }
    return TIKU_RADIO_PHY_1M;
}

/* Apply a requested dBm, reporting when the silicon refuses the step. */
static int rft_power(long dbm)
{
    if (dbm < -46 || dbm > 8) {
        SHELL_PRINTF("rftest: power %ld dBm out of range (-46..+8)\n", dbm);
        return -1;
    }
    if (tiku_radio_arch_set_txpower((int8_t)dbm) != 0) {
        SHELL_PRINTF("rftest: %ld dBm is not a silicon-legal step\n", dbm);
        return -1;
    }
    return 0;
}

static void rft_usage(void)
{
    SHELL_PRINTF("usage:\n");
    SHELL_PRINTF("  rftest cw <mhz> [dbm] [phy]    unmodulated carrier\n");
    SHELL_PRINTF("  rftest mod <mhz> [dbm] [phy]   modulated carrier\n");
    SHELL_PRINTF("  rftest sweep <lo> <hi> [dbm]   step across a range\n");
    SHELL_PRINTF("  rftest off                     stop transmitting\n");
    SHELL_PRINTF("  rftest status                  what is on air\n");
    SHELL_PRINTF("mhz %u..%u, dbm -46..+8 (default 0), "
                 "phy 1m|2m|s8|s2 (default 1m)\n",
                 RFT_MHZ_MIN, RFT_MHZ_MAX);
}

/* Name the RADIO.STATE codes that matter on a bench.  Reported straight
 * from the peripheral so a session can tell "the driver thinks it is
 * on" from "the hardware really is ramped up and radiating". */
static const char *rft_state_name(uint32_t st)
{
    switch (st) {
    case 0x0u:  return "DISABLED";
    case 0x1u:  return "RXRU";
    case 0x2u:  return "RXIDLE";
    case 0x3u:  return "RX";
    case 0x9u:  return "TXRU";
    case 0xAu:  return "TXIDLE (carrier radiating)";
    case 0xBu:  return "TX (modulating)";
    case 0xCu:  return "TXDISABLE";
    default:    return "?";
    }
}

static void rft_status(void)
{
    uint32_t st = tiku_radio_arch_state();

    if (tiku_radio_arch_carrier_active()) {
        SHELL_PRINTF("rftest: ON AIR %u MHz %s %d dBm "
                     "(radio busy until 'rftest off')\n",
                     rft_mhz, rft_modulated ? "modulated" : "unmodulated",
                     tiku_radio_arch_txpower());
    } else {
        SHELL_PRINTF("rftest: idle\n");
    }
    SHELL_PRINTF("rftest: RADIO.STATE = 0x%x %s\n",
                 (unsigned)st, rft_state_name(st));
}

static void rft_start(const char *mhz_s, const char *dbm_s,
                      const char *phy_s, int modulated)
{
    const char *phy_name;
    tiku_radio_arch_phy_t phy;
    long mhz;

    mhz = rft_atoi(mhz_s);
    if (mhz < (long)RFT_MHZ_MIN || mhz > (long)RFT_MHZ_MAX) {
        SHELL_PRINTF("rftest: frequency must be %u..%u MHz\n",
                     RFT_MHZ_MIN, RFT_MHZ_MAX);
        return;
    }
    if (rft_power(rft_atoi_signed(dbm_s, 0)) != 0) {
        return;
    }
    phy = rft_phy(phy_s, &phy_name);

    if (tiku_radio_arch_carrier_start(phy, (uint16_t)mhz, modulated) != 0) {
        SHELL_PRINTF("rftest: could not start (ramp-up timeout)\n");
        return;
    }
    rft_mhz       = (uint16_t)mhz;
    rft_modulated = (uint8_t)(modulated != 0);

    SHELL_PRINTF("rftest: %s carrier ON at %ld MHz, %d dBm, %s\n",
                 modulated ? "modulated" : "unmodulated", mhz,
                 tiku_radio_arch_txpower(), phy_name);
    SHELL_PRINTF("rftest: radio held -- 'rftest off' to release it\n");
}

/* Step a carrier across [lo, hi] a MHz at a time, dwelling briefly on
 * each step so a spectrum analyser in max-hold paints the whole span.
 * Blocking by design: it is a bench sweep, not a background service. */
static void rft_sweep(const char *lo_s, const char *hi_s, const char *dbm_s)
{
    long lo = rft_atoi(lo_s);
    long hi = rft_atoi(hi_s);
    long f;

    if (lo < (long)RFT_MHZ_MIN || hi > (long)RFT_MHZ_MAX || lo > hi) {
        SHELL_PRINTF("rftest: bad range (need %u <= lo <= hi <= %u)\n",
                     RFT_MHZ_MIN, RFT_MHZ_MAX);
        return;
    }
    if (rft_power(rft_atoi_signed(dbm_s, 0)) != 0) {
        return;
    }

    SHELL_PRINTF("rftest: sweeping %ld..%ld MHz at %d dBm\n",
                 lo, hi, tiku_radio_arch_txpower());
    for (f = lo; f <= hi; f++) {
        if (tiku_radio_arch_carrier_start(TIKU_RADIO_PHY_1M,
                                          (uint16_t)f, 0) != 0) {
            SHELL_PRINTF("rftest: start failed at %ld MHz\n", f);
            break;
        }
        {   /* Dwell ~20 ms per step, kicking the WDT as we go. */
            tiku_clock_time_t t0 = tiku_clock_time();
            while ((tiku_clock_time() - t0) <
                   (tiku_clock_time_t)(TIKU_CLOCK_SECOND / 50u)) {
                tiku_watchdog_kick();
            }
        }
        tiku_radio_arch_carrier_stop();
    }
    SHELL_PRINTF("rftest: sweep done, radio released\n");
}

void tiku_shell_cmd_rftest(uint8_t argc, const char *argv[])
{
    if (argc < 2u) {
        rft_status();
        rft_usage();
        return;
    }

    if (strcmp(argv[1], "cw") == 0) {
        rft_start(argc > 2u ? argv[2] : (const char *)0,
                  argc > 3u ? argv[3] : (const char *)0,
                  argc > 4u ? argv[4] : (const char *)0, 0);
    } else if (strcmp(argv[1], "mod") == 0) {
        rft_start(argc > 2u ? argv[2] : (const char *)0,
                  argc > 3u ? argv[3] : (const char *)0,
                  argc > 4u ? argv[4] : (const char *)0, 1);
    } else if (strcmp(argv[1], "sweep") == 0) {
        rft_sweep(argc > 2u ? argv[2] : (const char *)0,
                  argc > 3u ? argv[3] : (const char *)0,
                  argc > 4u ? argv[4] : (const char *)0);
    } else if (strcmp(argv[1], "off") == 0) {
        if (tiku_radio_arch_carrier_active()) {
            tiku_radio_arch_carrier_stop();
            SHELL_PRINTF("rftest: carrier off, radio released\n");
        } else {
            SHELL_PRINTF("rftest: nothing transmitting\n");
        }
    } else if (strcmp(argv[1], "status") == 0) {
        rft_status();
    } else {
        rft_usage();
    }
}

#endif /* TIKU_SHELL_CMD_RFTEST */
